# SPDX-License-Identifier: MIT
# DEV-287 acceptance driver: the product API, hub.reset_pico() (write + result read). From Explorer Sue's bench.
from noknok import Conductor
import time
c = Conductor(); c.enumerate()
hub = c.picohub
print("PM hub", hub and hex(hub.address))
print("RESET_PICO via hub.reset_pico()")
r = hub.reset_pico(ms=100)
print("result", r)
time.sleep(2)
print("STILL ALIVE: no reset happened")
