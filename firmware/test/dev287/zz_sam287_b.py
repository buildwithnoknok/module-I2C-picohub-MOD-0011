# SPDX-License-Identifier: MIT
# DEV-287 (Explorer Sam): RESET_PICO WRITE-ONLY (no result read) to the power manager at the address the
# Conductor really assigned (the PM is not always at 0x0a). MODE "b": immediately followed by a GET_STATUS write.
MODE = "b"
from noknok import Conductor
import time
c = Conductor(); c.enumerate()
hub = c.picohub
i2c, a = hub.i2c, hub.address
while not i2c.try_lock(): pass
i2c.writeto(a, bytes([0xB1])); v = bytearray(4); i2c.readfrom_into(a, v)
i2c.writeto(a, bytes([0x10])); b = bytearray(20); i2c.readfrom_into(a, b)
print("PM at", hex(a), "fw %d.%d.%d" % (v[1], v[2], v[3]), "pd", b[1], "vin", b[6] | b[7] << 8, "reset", b[18])
print("RESET_PICO now, write only, mode", MODE)
i2c.writeto(a, bytes([0x22, 100, 0, 0x5A]))
if MODE == "b":
    i2c.writeto(a, bytes([0x10]))
i2c.unlock()
time.sleep(2)
print("STILL ALIVE: no reset happened")
