# SPDX-License-Identifier: MIT
# Copyright (c) 2026 noknok
"""MOD-0011 power manager - I2C interface bench test (DEV-122). Runs on the brain (CircuitPython).

On the Pi4:  python3 pico.py run picohub_i2c_test.py 120
Bus A = GP10 (SDA) / GP11 (SCL). Finds the X035 at the enumeration address 0x7F (and assigns it
TEST_ADDR) or, if it is already assigned, by probing 0x08..0x77 for a power manager. Then checks
every command of the command map (module README). Prints PASS/FAIL per check and a summary.
Set RESET_PICO_AT_END = True to finish with RESET_PICO (the Pico reboots; the script ends there).
"""
import time
import board
import busio

TEST_ADDR = 0x60
RESET_PICO_AT_END = False
KEY = 0x5A

i2c = busio.I2C(board.GP11, board.GP10, frequency=100000)
while not i2c.try_lock():
    pass

results = []


def check(name, ok, info=""):
    results.append((name, ok))
    print("%s  %s  %s" % ("PASS" if ok else "FAIL", name, info))


def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def write(addr, data):
    i2c.writeto(addr, bytes(data))


def read(addr, n):
    buf = bytearray(n)
    i2c.readfrom_into(addr, buf)
    return buf


def cmd_read(addr, cmd, n):
    write(addr, [cmd])
    return read(addr, n)


def cmd_result(addr, data):
    write(addr, data)
    return read(addr, 1)[0]


def u16(b, i):
    return b[i] | (b[i + 1] << 8)


def status(addr):
    for _ in range(2):   # one retry on a bad CRC, as the brain driver will do
        b = cmd_read(addr, 0x10, 20)
        if crc8(b[:19]) == b[19]:
            t = u16(b, 12)
            if t >= 0x8000:
                t -= 0x10000
            return {"fmt": b[0], "pd": b[1], "mv": u16(b, 2), "ma": u16(b, 4), "vin": u16(b, 6),
                    "iin": u16(b, 8), "v5": u16(b, 10), "temp": t / 10, "rails": b[14], "inputs": b[15],
                    "faults": b[16], "notices": b[17], "reset": b[18]}
    return None


def find_power_manager():
    t0 = time.monotonic()
    while time.monotonic() - t0 < 4:   # enumeration: poll 0x7F every 20 ms (Conductor style)
        try:
            b = read(0x7F, 10)
            if b[8] == 0x07 and crc8(b[:9]) == b[9]:
                uid = "".join("%02x" % x for x in b[:8])
                write(0x7F, [0x1D, TEST_ADDR])
                time.sleep(0.05)
                return TEST_ADDR, "enumerated (UID %s), assigned 0x%02x" % (uid, TEST_ADDR)
        except OSError:
            pass
        time.sleep(0.02)
    for a in range(0x08, 0x78):        # already assigned?
        try:
            v = cmd_read(a, 0xB1, 4)
            s = status(a)
            if v[0] == 1 and s and s["fmt"] == 1:
                return a, "already assigned at 0x%02x" % a
        except OSError:
            pass
    return None, "not found"


addr, how = find_power_manager()
check("found the power manager (type 0x07)", addr is not None, how)
if addr is None:
    raise SystemExit

v = cmd_read(addr, 0xB1, 4)
check("GET_VERSION 0xB1 (4 bytes)", v[0] == 1, "protocol %d, firmware %d.%d.%d" % (v[0], v[1], v[2], v[3]))

s = status(addr)
check("GET_STATUS 0x10 CRC + format 1", s is not None and s["fmt"] == 1, str(s))
check("bus A reported on", s and s["rails"] & 0x01)

caps = cmd_read(addr, 0x11, 30)
n = caps[0]
ok = n <= 7 and crc8(caps[:1 + 4 * n]) == caps[1 + 4 * n]
check("GET_SOURCE_CAPS 0x11 CRC", ok, ", ".join("%d mV/%d mA" % (u16(caps, 1 + 4 * i), u16(caps, 3 + 4 * i)) for i in range(min(n, 7))))

check("unknown command -> 0xFF", cmd_result(addr, [0x7A]) == 0xFF)
check("SET_RAIL BUS_A (id 0) refused", cmd_result(addr, [0x20, 0, 0]) == 3)
check("SET_RAIL bad want refused", cmd_result(addr, [0x20, 1, 7]) == 3)
check("SET_RAIL bus B on -> applied", cmd_result(addr, [0x20, 1, 1]) == 0)
r = cmd_result(addr, [0x20, 2, 1])
s = status(addr)
sink = s and s["inputs"] & 0x01
check("SET_RAIL J2 on: applied with a device, interlock without", r == (0 if sink else 1), "result %d, J2 device %s" % (r, bool(sink)))
check("SET_RAIL J2 auto", cmd_result(addr, [0x20, 2, 2]) == 0)
check("SET_RAIL MEZZ auto", cmd_result(addr, [0x20, 3, 2]) == 0)

check("CYCLE_RAIL without key refused", cmd_result(addr, [0x21, 0, 250, 0, 0]) == 3)
check("RESET_PICO without key refused", cmd_result(addr, [0x22, 100, 0, 0]) == 3)
check("SET_MIN_INPUT_VOLTAGE 12 V refused", cmd_result(addr, [0x23, 12]) == 3)

# bus A power cycle, 3 times: the X035 must restore bus A and answer at the same address
for i in range(3):
    r = cmd_result(addr, [0x21, 0, 250, 0, KEY])
    time.sleep(0.6)
    try:
        s = status(addr)
        back = s is not None and s["rails"] & 0x01
    except OSError:
        back = False
    check("CYCLE_RAIL bus A 250 ms #%d: restored, X035 answers" % (i + 1), r == 0 and back)

# minimum input voltage: 20 V even with no load, then release
pdo_mv = [u16(caps, 1 + 4 * i) for i in range(min(n, 7))]
r = cmd_result(addr, [0x23, 20])
time.sleep(1.5)
s = status(addr)
want = 20000 if 20000 in pdo_mv else max([m for m in pdo_mv if m <= 20000] or [5000])
check("SET_MIN_INPUT_VOLTAGE 20 -> contract", s and s["mv"] == want and s["inputs"] & 0x08,
      "result %d, contract %s mV, V_IN %s mV" % (r, s and s["mv"], s and s["vin"]))
r = cmd_result(addr, [0x23, 0])
time.sleep(1.5)
s = status(addr)
check("SET_MIN_INPUT_VOLTAGE 0 -> back to 9 V", s and s["mv"] == (9000 if 9000 in pdo_mv else s["mv"]) and not s["inputs"] & 0x08,
      "contract %s mV" % (s and s["mv"]))

check("CLEAR_FAULTS", cmd_result(addr, [0x12]) == 0)
s = status(addr)
check("final status readable", s is not None, str(s))

passed = sum(1 for _, ok in results if ok)
print("SUMMARY %d/%d passed" % (passed, len(results)))

if RESET_PICO_AT_END:
    print("RESET_PICO with key: the Pico reboots in ~50 ms")
    write(addr, [0x22, 100, 0, KEY])
i2c.unlock()
