# MOD-0011 (noknok PicoHub) power manager: test plan

SPDX-License-Identifier: CC-BY-SA-4.0
Owner: Bob (QA). Story DEV-125. First version 22 Oct 2026. Basis: ADR-004 (Confluence 126615553), DEV-121,
DEV-122, DEV-124 acceptance lists, and the bench runs of 19-22 Oct 2026.

Status words: **DONE** = run and passed (where, when) · **OPEN** = not run yet, ticket named ·
**PoC** = must pass before a Proof-of-Concept unit is handed to anyone but Christopher.

## 1. Proof-of-Concept safety gate

A PoC unit may leave the bench only under these rules, until the whole plan has run:

1. Run only the tested envelope: **9 V / 3 A**, optionally 12 V / 2 A and 15 V / 2 A once DEV-143 lands. **No 20 V.**
2. Run only under supervision. No unattended overnight run, no children, no unattended charger.
3. Use only chargers that passed section 2 item 1 (no no-name PD chargers).
4. Fuse (2 A fast-blow) in line when a load is attached to J2 or the mezzanine 5 V.
5. After any firmware change: re-run the PoC items below.

PoC items (all must be DONE with the build that is handed out):

| # | Item | Status |
|---|---|---|
| P1 | PD cold start gives 9 V; LED one blink every 4 s | DONE (Sam 19 Oct, Bob 19-22 Oct, ~15 starts) |
| P2 | 5 V-only adapter: board runs, "limited power" notice only, J2 held off, Pico + 4 modules answer | DONE (19 Oct) |
| P3 | J2 interlocks: sink on -> VBUS on, off -> off; charger on J2 stays off (no back-feed); MEZZ_PRESENT -> J2 off, MEZZ_5V on; MEZZ_FAULT -> MEZZ_5V off, back after 1 s | DONE (19 Oct) |
| P4 | Over-current at 3 A / 9 V latches the fault, board keeps running | DONE (21 Oct, 3192 mA, no reset) |
| P5 | I_IN, V_IN, V_5V against a meter (1 % / 30 mA) | DONE (19, 21 Oct) |
| P6 | Brain integration: enumeration, status, cycle_rail, reset_pico, OTA of the X035 with a power pull | DONE (DEV-123, DEV-124) |
| P7 | NTC heating thresholds with the real sensor (75 / 85 / 95 C) | **OPEN** DEV-144 item 1 |
| P8 | Forced-hang watchdog build: X035 resets, board stays up | **OPEN** DEV-144 item 2 |
| P9 | 20 cold power pulls at 9 V, same contract every time | **OPEN** DEV-144 item 3 |
| P10 | Other chargers: laptop, power bank, 12 V / 20 W, 15 V PD | **OPEN** DEV-144 item 4 |
| P11 | Over-current shedding visible on J2 / MEZZ_EN with a real device | **OPEN** DEV-144 item 5 |
| P12 | Thermal soak 1 h at 9 V / 3 A (thermocouples on U2, L1, J1, NTC log) | **OPEN** DEV-146 |
| P13 | PoC power profile (DEV-143): contract never above the profile | **OPEN** DEV-143 |

## 2. Full plan (ADR-004 test outline, Q4)

| # | Area | What | Equipment | Status |
|---|---|---|---|---|
| 1 | PD source variety | 5 V-only adapter, C-to-C 5 V, PD 9/15/20 V, PPS, laptop, power bank, 12 V charger: contract, rails, LED, notices | chargers, KM003C | 5 V-only + 20 V charger DONE; rest OPEN (DEV-144 item 4) |
| 2 | Power pull and brown-out | at 5 and 9 V, >= 50 cycles, board returns with the same contract; at 20 V after the scope arrives | relay or Pi hub, KM003C; later bench supply | 5 cold starts + 2 DONE; 50 cycles OPEN |
| 3 | Interlocks | MEZZ_PRESENT, MEZZ_FAULT, J2 sink detect, bus A never off | jumper | DONE (P3); 10 ms MEZZ_FAULT timing OPEN (logic analyzer) |
| 4 | Faults | Qwiic port short (switch trips, X035 reports, others keep running), NTC simulated | fuse, resistor, hot air | NTC OPEN (P7); port short OPEN |
| 5 | Brain integration | enumeration with modules on both buses, telemetry, Pico reset, address stability after module power loss | Pico, modules | DONE except address stability (DEV-145 item 6) |
| 6 | Thermal soak | 1 h at 9 V / 3 A, 15 V / 2 A, then 20 V / 3 A | thermocouples, DL24P, later scope | OPEN (DEV-146, DEV-144 item 7) |
| 7 | Demand-based voltage | up at > 80 % of contract current for 2 s, down at < 60 % for 30 s, no oscillation | constant-power load >= 60 W | up/down 9<->15 V DONE (21 Oct); 15->20 V and no-oscillation OPEN (DEV-145 item 1) |
| 8 | Over-temperature (D8) | shed J2 + MEZZ_EN at 85 C with a minimum set, 3 sheds suspend the minimum | hot air | simulated DONE (Sam); real heating OPEN (DEV-145 item 3) |
| 9 | OTA of the X035 | normal update, power pull during the update (cut at 6 points), rescue of a parked X035, USB module found afterwards | Pi hub cut (`uhubctl -l 2`), `pullrun.sh` | DONE (DEV-124, DEV-134) |
| 10 | Load-dump robustness | abrupt load trip at 15-20 V (DEV-142) | scope on VBUS_PD | OPEN (DEV-142) |

## 3. Pass criteria and logging

* Every run writes the build (power manager version, brain code.py version, commit), the rig (Pi, Pico serial), counts ("n of m") and the log file name into the Jira verdict.
* A test that passes only sometimes is FLAKY and reported with its rate. A skipped step is reported as skipped.
* Failure of a safety item (P1-P13) blocks the PoC hand-out.

## 4. Equipment and what it unlocks

| Equipment | Status 22 Oct 2026 | Unlocks |
|---|---|---|
| XY-KZ35 constant-current load (35.5 W) | owned | 9 V / 3 A and 15 V / 2 A steps, over-current |
| KM003C, KWS-X1, KC0003V | owned | V/I/PD messages |
| 8-channel 24 MHz logic analyzer (sigrok) | owned | timing of pins and I2C; never on VBUS_PD |
| MAX31856 x5 + K-type probes | ordered | thermal soak, NTC calibration |
| DL24P 150 W load | ordered | 20 V x 3 A, constant-power mode, 5 V outputs |
| Programmable supply (KA3005P or RD6006) | planned | brownout and ramp tests |
| Oscilloscope with remote control (PicoScope 2206B class) | planned after the PoC | DEV-142 root cause, load-dump, 20 V soak |
| USB isolators ADUM3160 / ADUM4160 | owned, unused | only full-speed data links, no VBUS across |

Safety rules for the bench: never connect a logic analyzer or a scope ground clip to a net above 5 V
without a suitable probe; fuse the 5 V outputs; stop above 100 C on U2; do not leave the board
under load unattended; flash with J1 on a 5 V source (a halted chip made a PD charger latch off).

## 5. How to run

* Hands-on logger: `brain-Pico/tools/bench/bench_dev121_log.py` (changes with timestamps), `bench_dev122_load2.py` (adds reset cause and current peak), `bench_dev122_load3.py` (scheduled minimum voltage).
* I2C interface: `firmware/test/picohub_i2c_test.py` -> `SUMMARY 22/22`.
* OTA and rescue: `firmware/test/bl_x035_bench.py`, `brain-Pico/tools/bench/pullrun.sh`, `bench_dev134_park_x035.py`.
* Thermal: `firmware/test/picohub_thermal_log.py` (holds 20 V, logs every 10 s).
* The CH32V003 bootloader regression suite is separate: `module-I2C-bootloader/firmware/stage0/test/TESTS.md`.
