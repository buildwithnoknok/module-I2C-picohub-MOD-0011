# SPDX-License-Identifier: MIT
# Copyright (c) 2026 noknok
"""MOD-0011: does today's brain Conductor enumerate the power manager (type 0x07) next to modules?

Runs on the brain (CircuitPython, noknok.py on the Pico). On the Pi4:
    python3 pico.py run conductor_enum_test.py 60
The X035 must be at 0x7F (cold start, or debug key `e`). Expected with a noknok.py that has no
NoknokPicoHub driver yet: "unknown(0x07) -> 0x.." in the log, the other modules as usual.
"""
import board
import noknok

c = noknok.Conductor(sda=board.GP10, scl=board.GP11)
c.enumerate()
print("REGISTRY:")
for uid, m in c._registry.items():
    print("  %s -> %s" % (uid, "None (no driver)" if m is None else "%s @0x%02X" % (type(m).__name__, m.address)))
