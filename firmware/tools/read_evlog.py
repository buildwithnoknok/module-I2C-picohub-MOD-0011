# SPDX-License-Identifier: MIT
# Copyright (c) 2026 noknok
"""Decode the MOD-0011 power manager's PD event log, read from RAM over SWD.

The log lives at 0x20004000 (776 bytes) and survives a warm reset but not a power loss, so it
shows what happened at a cold start before a debugger was attached. On the Pi4:
    minichlink -r log.bin 0x20004000 776 && python3 read_evlog.py log.bin
Library states: 0 idle, 1 cable detected, 2 got caps, 3 wait accept, 4 wait PS_RDY, 5 PS_RDY.
Values: request/contract = mV, caps = PDO count, no-PD = Type-C mA, boot = reset flags >> 24
(0x18 power-on, 0x10 software, 0x20 watchdog), rails = 01 bus A 02 bus B 04 J2 08 MEZZ_EN
10 buck (hex), inputs = 01 J2 sink 02 mezzanine present 04 mezzanine fault 08 minimum set
10 minimum suspended, target = voltage the policy asks for (mV), cycle = rail 0 bus A 1 bus B 2 J2 3 MEZZ_EN.
"""
import struct
import sys

NAMES = ["?", "boot", "state", "caps", "request", "contract", "reject", "timeout",
         "soft-reset", "hard-reset", "no-PD", "PD-failed", "rails", "inputs", "i2c-addr", "pico-reset", "cycle", "target"]
N = 96

d = open(sys.argv[1], "rb").read()
magic, n = struct.unpack("<II", d[:8])
print("magic %08x (expect 4e4b5044), %d events" % (magic, n))
for k in range(max(0, n - N), n):
    ms, code, val = struct.unpack("<IHH", d[8 + (k % N) * 8: 16 + (k % N) * 8])
    name = NAMES[code] if code < len(NAMES) else str(code)
    shown = ("0x%02x" % val) if name in ("rails", "inputs", "boot", "i2c-addr") else str(val)
    if name == "cycle":
        shown = "rail %d for %d ms" % (val >> 13, val & 0x1FFF)
    print("%8d ms  %-10s %s" % (ms, name, shown))
