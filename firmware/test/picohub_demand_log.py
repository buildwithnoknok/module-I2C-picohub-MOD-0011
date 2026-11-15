# SPDX-License-Identifier: MIT
# Copyright (c) 2026 noknok
"""MOD-0011 power manager - status logger for load tests (DEV-143). Runs on the brain (CircuitPython).

Prints one line every 0.5 s: contract mV, V_IN, I_IN, faults, notices, inputs, PD state, X035 reset
cause (r: 0 power-on, 1 watchdog, 2 other; a change = the X035 restarted). Use it while an
electronic load draws from the board (bench: DL24P on the mezzanine cluster-A pads A4 = VBUS_PD,
A8 = GND, driven by dl24_run.py on agent-hq). At the start it clears latched faults and sets the fixed
input voltage FIXED (0 = demand); at the end it sets FIXED back to 0.

Needs /picohub_i2c_test.py on the Pico (its helpers are reused). Driver file on the Pico, e.g.:
    FIXED = 0          # or 9 / 12 / 15 / 20 to hold that level (SET_FIXED_INPUT_VOLTAGE 0x24)
    SECS = 170
    exec(open("/picohub_demand_log.py").read())
Run: python3 pico.py run drv.py 210 > log.txt   (then: sudo python3 ~/dl24_run.py 2.6 5 60 dl24.log)
Bench runs 12 Nov 2026: ladder 9 -> 15 -> 20 V at 2.6 A; over-current 3.1 A at fixed 9 / 15 / 20 V.
"""
PHASE = "none"
exec(open("/picohub_i2c_test.py").read().split("if PHASE in")[0])
print("fixed", FIXED, "result", cmd_result(addr, [0x24, FIXED]), "clear", cmd_result(addr, [0x12]))
t0 = time.monotonic()
while time.monotonic() - t0 < SECS:
    try:
        s = status(addr)
        print("L %5.1f mv %5d vin %5d iin %4d f %02x n %02x in %02x pd %d r %d" % (
            time.monotonic() - t0, s["mv"], s["vin"], s["iin"], s["faults"], s["notices"], s["inputs"], s["pd"],
            s["reset"]))
    except Exception as e:
        print("E %5.1f %s" % (time.monotonic() - t0, e))
    time.sleep(0.5)
print("end fixed 0 result", cmd_result(addr, [0x24, 0]))
