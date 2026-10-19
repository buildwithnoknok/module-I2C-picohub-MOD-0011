# SPDX-License-Identifier: MIT
# Copyright (c) 2026 noknok
#
# MOD-0011 bring-up (DEV-117 step 5): scan both I2C buses of the PicoHub from the Pico.
# Bus A = GP10 SDA / GP11 SCL (J3, J4, cluster C, X035)
# Bus B = GP20 SDA / GP21 SCL (J5, J6)
# Runs ~90 s, scans every second and prints only CHANGES with a timestamp, so one module
# can be moved from port to port while it runs.
# A noknok module that has not been enumerated sits at the staging address 0x7F, which
# i2c.scan() never probes (0x78-0x7F is reserved). So each pass also READS the 10-byte
# UID+CRC from 0x7F (step 1 of the handshake, harmless: no address is ever assigned).
# An unassigned module leaves 0x7F for 50-549 ms after 200 ms (re-backoff), so 0x7F is
# polled every 50 ms for ~1 s per pass. No writes to any device.
# Run on the Pi4: python3 pico.py run pico_port_scan.py 110
import time
import board
import busio

BUSES = (("A (J3/J4)", board.GP11, board.GP10),
         ("B (J5/J6)", board.GP21, board.GP20))
DURATION_S = 90

def scan(scl, sda):
    try:
        i2c = busio.I2C(scl, sda, frequency=100000)
    except Exception as e:
        # CircuitPython refuses to open a bus without pull-ups: on this board that
        # means the bus rail (+3V3_A / +3V3_B, which carries the pull-ups) is off.
        return "no bus (%s)" % e
    staged = None
    try:
        while not i2c.try_lock():
            pass
        found = i2c.scan()
        buf = bytearray(10)
        for _ in range(10):                      # ~0.5 s of 0x7F polling
            try:
                i2c.readfrom_into(0x7F, buf)
                staged = bytes(buf)
                break
            except OSError:
                time.sleep(0.05)
        i2c.unlock()
    finally:
        i2c.deinit()
    parts = ["0x%02X" % a for a in found]
    if staged:
        parts.append("0x7F module UID %s byte8 0x%02X" % ("".join("%02x" % b for b in staged[:8]), staged[8]))
    return " ".join(parts) if parts else "empty"

t0 = time.monotonic()
last = {}
print("scan start")
while time.monotonic() - t0 < DURATION_S:
    for name, scl, sda in BUSES:
        r = scan(scl, sda)
        if last.get(name) != r:
            print("%5.1f s  bus %s: %s" % (time.monotonic() - t0, name, r))
            last[name] = r
    time.sleep(1)
print("scan end")
