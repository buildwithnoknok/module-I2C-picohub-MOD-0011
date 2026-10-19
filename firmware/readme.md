# MOD-0011 power manager firmware (CH32X035)

Firmware for the CH32X035 on the noknok PicoHub (MOD-0011). Source: `src/picohub.c` (the header comment
holds the pin map of the board as fabricated). Version: see `index.json`; history: `../CHANGELOG.md`.
Design: ADR-004 and the design page "MOD-0011 power manager - I2C interface and input voltage policy".

The board works with **no firmware at all** (buses, buck and Pico run on any USB-C charger). The firmware
adds the USB-PD contract, the USB host port J2, the mezzanine 5 V, and protection.

## What it does (v0.3, DEV-121 + DEV-122 + DEV-124)

| Area | Behaviour |
| --- | --- |
| Charger (J1) | **Any USB-C charger works.** A 5 V-only adapter runs the board at 5 V: enough for small products; the LED shows *limited power*. With USB-PD the board takes 5 V first and steps to **9 V** when the charger offers it. Requests are capped at 3 A. |
| Cold start | The 5 V buck (and so the Pico) is held off while the contract is negotiated, at most 0.5 s. After a watchdog or software reset of the X035 the buck is NOT touched, so an X035 reset never reboots the Pico; the X035 then reads the charger's capabilities again with a USB-PD soft reset and keeps the voltage that is already there. |
| USB host port J2 | VBUS on only when a device (sink) is detected on its CC lines **and** no mezzanine has taken the USB uplink (MEZZ_PRESENT). A charger plugged into J2 is never powered. On a 5 V-only charger J2 needs one that advertises at least 1.5 A. |
| Mezzanine | MEZZ_EN (mezzanine 5 V and the mezzanine's enable line) goes on when MEZZ_PRESENT is asserted, and off at once on MEZZ_FAULT (back 1 s after the fault is released). |
| I2C buses | Bus A is never switched off by this firmware; bus B stays on (the brain can switch it from DEV-122 on). |
| Over-current | Input current above the contract's current (max 3 A) for 0.2 s: J2 off, then the mezzanine. The buck and the buses stay on so the Pico can keep running. Loads return 5 s after the current is back under 80 %. |
| Temperature (NTC next to the buck) | > 75 C notice, > 85 C J2 and the mezzanine off (back < 75 C), > 95 C buck off (back < 70 C). Three sheds within 10 minutes: the mezzanine stays off until the faults are cleared. The input voltage is never lowered for heat (that would raise the current through the board). |
| Watchdog | ~1 s independent watchdog. |
| Firmware update | Over I2C from the brain, through the noknok I2C bootloader (DEV-124), see "Firmware update" below. |

| Input voltage (v0.2) | Chosen by the board by demand: 9 V; up to 15 then 20 V when the input current stays above 80 % of the contract for 2 s; one level down when the input power stays below 60 % of the next lower level for 30 s. The brain may set a **minimum** (15/20 V) for a mezzanine that needs it; heat never lowers the voltage. |

**All thresholds are placeholders** until the load tests (DEV-117 step 6); they live at the top of
`src/picohub.c`.

## I2C interface (v0.3.0, DEV-122 + DEV-124) - the command map

This section is the authoritative spec of the power manager's I2C protocol. Design reasoning is
recorded in noknok's decision record ADR-004. Principle: everything inside the board is decided by the
board; the brain reads telemetry and sends steering commands, and the board refuses what a safety
rule forbids.

**Bus and enumeration.** The X035 sits on I2C bus A (PC16 = SCL, PC17 = SDA; Pico GP11/GP10) and is a
standard noknok module (Ecosystem `software/enumeration.md`): after a UID-based backoff
(300-2799 ms) it answers at **0x7F** with 10 bytes `[UID 8 bytes, type 0x07, CRC8]` and takes the
address the Conductor assigns with `[0x1D, addr]`. It keeps that address while it is powered
(always-on 3.3 V), also over a Pico reboot, a bus-A power cycle and a watchdog reset of the X035
itself. Only a power loss of the board makes it enumerate again.

**Conventions.** A command is one write: command byte + arguments. Commands that return data: write
the command byte, then read. Values little-endian. Multi-byte replies end with a **CRC8**
(polynomial 0x07, init 0, as in enumeration); on a bad CRC, retry once. A read after any other
command returns its **result byte**: 0 applied, 1 held off by an interlock, 2 held off by a fault or
too little power, 3 invalid (bad length, value or key), 0xFF unknown command.

| Cmd | Name | Write | Read |
| --- | --- | --- | --- |
| `0xB1` | GET_VERSION | `[0xB1]` | 4 B: protocol 1, major, minor, patch |
| `0xB0` | ENTER_BOOTLOADER | `[0xB0]` | result 0; ~20 ms later the X035 resets into its bootloader at `0x7E` (see "Firmware update") |
| `0x10` | GET_STATUS | `[0x10]` | 20 B, see below |
| `0x11` | GET_SOURCE_CAPS | `[0x11]` | 1 B count n, n x {mV u16, max mA u16} (fixed PDOs), CRC8 |
| `0x12` | CLEAR_FAULTS | `[0x12]` | result. Clears latched faults and a 3-sheds suspension |
| `0x20` | SET_RAIL | `[0x20, rail, want]` | result. rail 1 bus B, 2 J2 (USB host VBUS), 3 MEZZ_EN; want 0 off, 1 on, 2 auto. Bus A has no id |
| `0x21` | CYCLE_RAIL | `[0x21, rail, ms lo, ms hi, 0x5A]` | result. Off for ms, then back to the wish. rail 0 = bus A (50-2000 ms; the only way bus A ever goes off), 1-3 as above (50-5000 ms). Starts 20 ms after the write |
| `0x22` | RESET_PICO | `[0x22, ms lo, ms hi, 0x5A]` | result. PICO_RUN low for ms (10-1000), 50 ms after the write |
| `0x23` | SET_MIN_INPUT_VOLTAGE | `[0x23, volts]` | result. 0 = none, 15 or 20. Result 2 if the charger cannot reach it (the board takes the highest it can) |

**Wishes and interlocks.** Defaults after power-up: bus B on, J2 auto, MEZZ_EN auto. Whatever the
wish: J2 only with a device detected on its CC lines, never while a mezzanine asserts MEZZ_PRESENT,
on a 5 V-only charger only if it offers at least 1.5 A, off during an over-current or heat shed.
MEZZ_EN auto = on while MEZZ_PRESENT is asserted (a mezzanine that does not assert it is switched on
by its product, wish "on"); off on MEZZ_FAULT, over-current level 2, heat shed, 3-sheds suspension.
J2 "auto" with no device answers 0 (it will switch on when one comes); J2 "on" with no device answers 1.
Wishes and the minimum survive a Pico reboot and an X035 watchdog reset; a power loss restores the
defaults.

**GET_STATUS (20 bytes).**

| Byte | Field |
| --- | --- |
| 0 | format version, 1 |
| 1 | PD state: 0 no PD (Type-C current only), 1 negotiating, 2 PD contract, 3 PD failed (runs at 5 V) |
| 2-3 / 4-5 | contract mV / mA (no PD: 5000 mV and the Type-C current 500/1500/3000 mA; capped at 3 A) |
| 6-7 / 8-9 / 10-11 | V_IN mV / I_IN mA (resolution ~10 mA) / 5 V rail mV |
| 12-13 | temperature, signed, 0.1 C (NTC next to the buck) |
| 14 | rails on: 01 bus A, 02 bus B, 04 J2, 08 MEZZ_EN, 10 buck |
| 15 | inputs: 01 J2 device, 02 MEZZ_PRESENT, 04 MEZZ_FAULT, 08 minimum set, 10 minimum suspended (3 sheds) |
| 16 | faults, latched until CLEAR_FAULTS: 01 input over-current, 02 heat shed, 04 heat critical (buck was off), 08 mezzanine fault, 10 5 V rail low, 20 PD failed, 40 temperature sensor open/short |
| 17 | notices, live: 01 limited power (5 V-only charger, not an error), 02 weak charger (< 15 W), 04 hot, 08 J2 held off by MEZZ_PRESENT, 10 J2 held off for power, 20 minimum not met |
| 18 | X035 reset cause: 0 power-on, 1 watchdog, 2 other, 3 software/debugger |
| 19 | CRC8 of bytes 0-18 |

Bench test: `test/picohub_i2c_test.py` (runs on the Pico, 22 checks); `test/conductor_enum_test.py`
(the brain Conductor's view).

## Status LED (D4)

| LED | Meaning |
| --- | --- |
| on (first second) | negotiating with the charger |
| short blip every 4 s | healthy, USB-PD contract above 5 V |
| double blip every 2 s | limited power: 5 V-only charger (not an error) |
| fast blinking | protection active (over-current, heat, mezzanine fault) or USB-PD failed |

## Flash layout

Flash layout **3** (the CH32X035 port of the noknok I2C bootloader, repo `module-I2C-bootloader`,
`firmware/port/port_ch32x035.h` is the authoritative map):

| Address | Size | What |
| --- | --- | --- |
| `0x0000` | 1 KB | stage-0 (frozen at the factory) |
| `0x0400` | 4 KB | stage-1 bootloader (updatable over I2C) |
| `0x1400` | 56 KB | this app (`src/app.ld`), app header at `0x1600` |
| `0xF400` | 256 B | bootloader control block |
| `0xF500` | 256 B | app metadata (length + CRC32, written by the bootloader) |

Top 16 B of RAM (`0x20004FF0`) are the bootloader's no-init cells (handoff, boot-attempt counters);
the stack ends below them. The app's own RAM cells: event log `0x20004000`, keep cell `0x20004400`.

## Firmware update (DEV-124)

The brain updates the power manager like any noknok I2C module: it compares `0xB1` with this repo's
`index.json`, checks the bootloader's flash layout (3) against the image's, sends `0xB0`, then
ERASE / WRITE_CHUNK / VERIFY / BOOT at `0x7E` (protocol: Ecosystem `software/firmware-update.md`).
An app update takes about 3 s. Things specific to this board:

- **While the X035 sits in its bootloader the board keeps running on its hardware defaults:** buses
  A and B on, 5 V buck on (the Pico keeps running), J2 VBUS and MEZZ_EN off, no protection, no
  voltage changes. The charger keeps the contract it had; nothing answers USB-PD meanwhile (bench:
  120 s at a 9 V contract without the charger switching off). The new app re-reads the charger's
  capabilities with a PD soft reset when it starts.
- **Status LED in the bootloader:** slow even blink (0.5 s on / 0.5 s off).
- After a visit to the bootloader the app keeps the brain's wishes (bus B, J2, MEZZ_EN, minimum
  voltage) but enumerates again at `0x7F`; the brain gives it back its usual address.
- The bootloader refuses an image that is not an X035 app for layout 3 (error 9), so a module's
  image can never land on this chip. Power loss during an update leaves the X035 parked at `0x7E`;
  the brain's start-up rescue pushes the app again (bench: 7 power cuts during updates, 7 recoveries).
- After three watchdog resets in a row without the brain ever reaching the app, the bootloader stops
  starting it and waits at `0x7E` (error 7) for a new image.

Bench tests: `test/bl_x035_bench.py` (runs on the brain; update, chip guard, interrupted update,
stage-1 self-update, USB-PD silence, power-pull loop).

## Build and flash

Needs [ch32fun](https://github.com/cnlohr/ch32fun) (RISC-V GCC toolchain + `minichlink`) and a WCH-LinkE.

```bash
cd firmware/src
make CH32FUN_DIR=/path/to/ch32fun BOOTLOADER=/path/to/module-I2C-bootloader/firmware  # picohub.bin + picohub_swd.bin
make swd CH32FUN_DIR=/path/to/ch32fun  # flash picohub_swd.bin through J8 with the WCH-LinkE
```

Flash through **J8** (bottom side: GND, SWDIO, 3V3, SWCLK) with the board powered on J1 from a plain
**5 V** source, not a USB-PD charger with a contract running: while the debugger halts the chip nothing
answers PD, and some chargers then switch off until they are unplugged. Do not power the board from
the programmer (`-3`). Verify with a readback
(`minichlink -r rb.bin flash <size>` and `cmp`). `bin/picohub_swd.bin` is the image to flash on a
bench or in the factory: stage-0 + stage-1 + app + metadata over the whole 62 KB, so nothing stale
survives. `bin/picohub.bin` is the app alone, linked at `0x1400`: the over-the-air update image. The
bootloader binaries come from repo `module-I2C-bootloader` (`firmware/bin/noknok_stage0_x035.bin`,
`noknok_stage1_x035.bin`). After the factory flash, updates go over I2C; SWD is only for recovery.

## Event log (read after the fact)

Attaching a debugger resets the X035, so what happened at a cold start is kept in a RAM event log
(96 entries at 0x20004000): PD states, capabilities, requests, contracts, timeouts, and every change
of the rails and the J2/mezzanine inputs, each with a millisecond timestamp. It survives warm resets,
not a power loss. Read it without the terminal:

```bash
minichlink -r log.bin 0x20004000 776 && python3 ../tools/read_evlog.py log.bin
minichlink -b    # the read halts the core: let it run again
```

## Debug terminal

`minichlink -T`, or `tools/x035_term.sh` (a persistent `minichlink -T` session for scripted use; keys via
`/tmp/x035_in.txt`, output in `/tmp/x035_out.log`) shows a status line every 2 s:
contract, V_IN, V_5V, I_IN, temperature, J2 CC lines, and the rails / inputs / faults / notices bytes
(bit meanings: key `h`). Nothing is printed while no debugger is attached.

| Key | Action |
| --- | --- |
| `s` | status now |
| `p` | J1 CC levels (which comparator thresholds each CC line is above) + PD receive/transmit counters |
| `v` | minimum input voltage 0 -> 15 -> 20 -> 0 V (same as SET_MIN_INPUT_VOLTAGE) |
| `e` | forget the I2C address and enumerate again (bench) |
| `c` | clear latched faults (and a 3-sheds suspension) |
| `t` / `T` | add 5 C to the temperature reading (simulated heat) / remove the offset |
| `w` | stop feeding the watchdog: the X035 resets after ~1 s (the board keeps running) |
| `h` | help |
