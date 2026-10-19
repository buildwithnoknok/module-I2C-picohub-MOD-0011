# SPDX-License-Identifier: MIT
# Copyright (c) 2026 noknok
#
# MOD-0011 bring-up (DEV-117 step 5): is a USB module on J2 visible to the Pico?
# PIO-USB host on GP0 (D+) / GP1 (D-), the MOD-0011 as-built pins. J2 VBUS must be switched
# on first (X035 bring-up firmware, key `u`). Lists every USB device for up to 15 s; for a
# noknok module (VID 0x1209, PID 0x4E4E) asks the type (0xF0) and version (0xB1). Read-only.
# Run on the Pi4: python3 pico.py run pico_usb_probe.py 30
import time
import board
import usb_host
import usb.core

port = usb_host.Port(board.GP0, board.GP1)
print("host port up on GP0/GP1")

def query(dev, cmd, n):
    dev.write(0x02, bytes([cmd]), timeout=1000)
    buf = bytearray(n)
    got = dev.read(0x83, buf, timeout=500)
    return bytes(buf[:got])

t0 = time.monotonic()
seen = set()
while time.monotonic() - t0 < 15:
    for d in usb.core.find(find_all=True):
        key = (d.idVendor, d.idProduct, getattr(d, "serial_number", None))
        if key in seen:
            continue
        seen.add(key)
        print("%5.1f s  device %04x:%04x  %s / %s  serial %s" % (
            time.monotonic() - t0, d.idVendor, d.idProduct,
            getattr(d, "manufacturer", "?"), getattr(d, "product", "?"), key[2]))
        if d.idVendor == 0x1209 and d.idProduct == 0x4E4E:
            try:
                d.set_configuration()
            except Exception:
                pass
            try:
                t = query(d, 0xF0, 3)
                v = query(d, 0xB1, 4)
                print("         type reply %s  -> module type 0x%02X" % (t.hex(), t[2] if len(t) == 3 else -1))
                if len(v) == 4:
                    print("         firmware %d.%d.%d (protocol %d)" % (v[1], v[2], v[3], v[0]))
            except Exception as e:
                print("         query failed: %r" % e)
    time.sleep(0.5)
print("probe end, %d device(s)" % len(seen))
