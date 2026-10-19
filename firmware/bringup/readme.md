# MOD-0011 bring-up firmware (v0.0.x)

Bench-only firmware for the CH32X035 on the noknok PicoHub rev 1.0 (DEV-117). It is **not** the
power-manager firmware (that waits for the DEV-85 decisions). It answers four questions:
does the X035 come back after a warm reset, what do the rails do with no firmware influence,
do the enable lines work, and do the ADC readings match a multimeter. Source: `bringup.c` (the
header comment holds the full pin map, taken from the fabricated PCB). Prebuilt: `bringup.bin`.

## Build and flash

Needs [ch32fun](https://github.com/cnlohr/ch32fun) and a WCH-LinkE.

```bash
make bringup.bin CH32FUN_DIR=/path/to/ch32fun   # build only
make flash CH32FUN_DIR=/path/to/ch32fun         # build + flash through the WCH-LinkE
minichlink -T                    # debug terminal: prints, and type a key + Enter for commands
```

Flash through **J8** (bottom side, the standard 4-pad USB-module flash pattern: GND, SWDIO,
3V3, SWCLK) with the flash clamp.

## Power for the reset test: USB-C J1, not the clamp alone

The test only means something when bus A is powered, because then SDA_A (PC17, the BOOT pin) is
pulled high during the reset. Feed the board from a USB-C source on **J1** (a plain 5 V
supply is enough). If PC17 reads low, the firmware does not run the test: the LED blinks fast and
it waits.

## Reading the result from the LED (no debugger needed)

| LED | Meaning |
| --- | --- |
| 1 blink, three times | cold start, software reset follows in 3 s |
| 2 blinks, three times, then short flashes | came back from the software reset; watchdog reset after about 5.6 s (the X035 watchdog runs from HSI/1024 = 46.9 kHz; v0.0.1 printed ~2 s, fixed in v0.0.2) |
| 3 blinks, three times, then a 1 s heartbeat | came back from the watchdog reset: **PASS** |
| dark after a reset, and stays dark | stuck in the factory bootloader: **FAIL** |
| fast blinking | test paused: PC17 low, bus A not powered |

To repeat the test without unplugging: type `r` in `minichlink -T`. Also repeat it once with the
WCH-LinkE disconnected, so the debugger cannot influence the reset.

## Sending commands from a script

`minichlink -T` takes keys only from a terminal, never from a pipe. `x035_term.sh` (set `MINICHLINK` to
your minichlink binary) starts one persistent session inside `script`: send keys with `printf a >> /tmp/x035_in.txt`,
read `/tmp/x035_out.log`. Starting it reboots the X035 (the reset test runs again, about 17 s).

## Monitor commands (after PASS)

| Key | Action |
| --- | --- |
| `a` / `b` | toggle EN_A / EN_B (bus A / bus B 3.3 V) |
| `u` | toggle EN_USB (J2 VBUS). Refused while a mezzanine is present |
| `m` | toggle MEZZ_EN (mezzanine 5 V) |
| `k` | toggle EN_BUCK: the whole 5 V rail, **including the Pico** |
| `p` | pulse PICO_RUN low for 100 ms (resets the Pico) |
| `0` | release every control line (back to the hardware defaults) |
| `d` | raw dump of ADC channels 0 to 15 |
| `r` | run the reset test again |
| `h` | help |

No control line is touched until a key is pressed, so measure the hardware default states first.
The status line every second shows V_IN, V_5V, I_IN, CC1/CC2 (J2 host port), the board
temperature from NTC TH1 (next to U2) and the digital lines.
