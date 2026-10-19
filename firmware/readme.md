# MOD-0011 power manager firmware (CH32X035)

Firmware for the CH32X035 on the noknok PicoHub (MOD-0011). Source: `src/picohub.c` (the header comment
holds the pin map of the board as fabricated). Version: see `index.json`; history: `../CHANGELOG.md`.
Design: ADR-004 and the design page "MOD-0011 power manager - I2C interface and input voltage policy".

The board works with **no firmware at all** (buses, buck and Pico run on any USB-C charger). The firmware
adds the USB-PD contract, the USB host port J2, the mezzanine 5 V, and protection.

## What it does (v0.2, DEV-121 + DEV-122)

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

| Input voltage (v0.2) | Chosen by the board by demand: 9 V; up to 15 then 20 V when the input current stays above 80 % of the contract for 2 s; one level down when the input power stays below 60 % of the next lower level for 30 s. The brain may set a **minimum** (15/20 V) for a mezzanine that needs it; heat never lowers the voltage. |

**All thresholds are placeholders** until the load tests (DEV-117 step 6); they live at the top of
`src/picohub.c`.

## I2C interface (v0.2.0, DEV-122) - the command map

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
| `0xB0` | ENTER_BOOTLOADER | `[0xB0]` | result 3 until the X035 bootloader exists (DEV-124) |
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

The app is linked at **0x1400**, the noknok bootloader's app base (same layout as the CH32V003
modules), so it needs no relink when the X035 bootloader (DEV-124) arrives. Until then
`tools/mk_swd_image.py` puts a 4-byte jump (`j 0x1400`) at 0x0000. Top 16 B of RAM
(0x20004FF0) are reserved for the bootloader handoff cell; the last 1 KB of flash (0xF400) for
the bootloader metadata.

## Build and flash

Needs [ch32fun](https://github.com/cnlohr/ch32fun) (RISC-V GCC toolchain + `minichlink`) and a WCH-LinkE.

```bash
cd firmware/src
make CH32FUN_DIR=/path/to/ch32fun    # picohub.bin (app @0x1400) + picohub_swd.bin (stub + app)
make swd CH32FUN_DIR=/path/to/ch32fun  # flash picohub_swd.bin through J8 with the WCH-LinkE
```

Flash through **J8** (bottom side: GND, SWDIO, 3V3, SWCLK) with the board powered on J1 from a plain
**5 V** source, not a USB-PD charger with a contract running: while the debugger halts the chip nothing
answers PD, and some chargers then switch off until they are unplugged. Do not power the board from
the programmer (`-3`). Verify with a readback
(`minichlink -r rb.bin flash <size>` and `cmp`). `bin/picohub_swd.bin` is the image to flash on a
bench or in the factory; `bin/picohub.bin` is the app alone (the future OTA payload).

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
