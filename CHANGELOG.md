# Changelog - noknok PicoHub (MOD-0011)

Newest first. One entry per version bump, written in the same commit as the bump. Format:
`## <version> - <date>` then `- DEV-xx - what changed and why. Breaking: yes/no.`
Convention: Confluence "Development Conventions" (SD space, page 120160257).

## 0.2.0 - 2026-10-19

- DEV-122 - The power manager is now a standard noknok I2C module on bus A (`src/i2c_module.h`):
  enumerates at 0x7F as type 0x07, 4-byte GET_VERSION, GET_STATUS (20 B + CRC8), GET_SOURCE_CAPS,
  CLEAR_FAULTS, SET_RAIL (bus B, J2, MEZZ_EN wishes; the board's interlocks always win), CYCLE_RAIL
  (bus A only as a timed, self-restoring cycle) and RESET_PICO (both need key 0x5A),
  SET_MIN_INPUT_VOLTAGE (15/20 V or none). Input voltage chosen by demand (9 -> 15 -> 20 V and back,
  placeholder thresholds); three heat sheds in 10 min suspend MEZZ_EN and the minimum until cleared.
  The assigned address and the brain's wishes survive an X035 watchdog reset. Command map in
  `firmware/readme.md`; bench tests `test/picohub_i2c_test.py` (22 checks) and
  `test/conductor_enum_test.py`. Spec section 6: MEZZ_EN gating is now mandatory for mezzanines
  powered from cluster A. Breaking: no.

## 0.1.0 - 2026-10-19

- DEV-121 - First power-manager firmware for the CH32X035 (`firmware/src/`, ADR-004). USB-PD sink:
  cold start holds the 5 V buck through negotiation, takes 5 V, releases the buck, then steps to 9 V
  (9 V contract ~650 ms after power-up on the bench); any charger works, a 5 V-only one runs the board
  with a "limited power" notice. Warm resets (watchdog, debugger) never touch the buck and re-read the
  charger with a PD soft reset. J2 VBUS only with a sink on its CC lines and no mezzanine present (and
  >= 1.5 A on a 5 V-only source); MEZZ_EN on with MEZZ_PRESENT, off on MEZZ_FAULT. Protection:
  input over-current, temperature (notice / shed / buck off, no voltage step-down), 5 V under-voltage,
  3 sheds in 10 min suspend MEZZ_EN; thresholds are placeholders until the load tests. Status LED,
  ~1 s watchdog, PD event log in RAM. App linked at 0x1400 (noknok bootloader app base) behind a jump
  stub. Vendored ch32fun USB-PD library with two bench-found fixes: never transmit while the GoodCRC
  for the source's capabilities is still going out, and 20 us comparator settling so a source on CC2
  (plug turned over) is detected. Breaking: no.

## Bring-up firmware 0.0.2 - 2026-10-17

- DEV-117 - RAM boot log (every reset cause since power-up, printed in the banner). A cold start is now
  recognised by the POWER-ON flag: the X035 factory bootloader starts the app with a software reset at
  every power-up, so 0.0.1 mistook a cold start for step 2. Watchdog text corrected to ~5.6 s (IWDG runs
  from HSI/1024). Breaking: no.

## Bring-up firmware 0.0.1 - 2026-10-17

- DEV-117 - First firmware for the CH32X035: bench bring-up only (`firmware/bringup/`), not the
  power-manager firmware. Runs the PC17/BOOT warm-reset test on its own at power-on (cold start,
  then software reset, then watchdog reset), leaves every control line at its hardware default
  until a command is typed, toggles EN_A / EN_B / EN_USB / MEZZ_EN / EN_BUCK and pulses
  PICO_RUN on command, and prints V_IN, V_5V, I_IN, CC sense and the NTC board temperature over
  the debug link. Run on the bench 17 Oct 2026: BOOT/warm-reset test PASS. Breaking: no.
