# SPDX-License-Identifier: MIT
# Copyright (c) noknok
#
# picohub_thermal_log.py - DEV-117 step 6 (MOD-0011 thermals). Runs ON the Pico fitted to the board,
# talks raw I2C to the power manager on bus A (GP10/GP11), so stop the product first (pico.py run does).
#   - holds the input contract at MIN_VOLTS with SET_MIN_INPUT_VOLTAGE (0 = leave it to the board)
#   - prints one line every SAMPLE_S seconds: contract, V_IN, I_IN, V_5V, NTC temperature (TH1, next
#     to the buck U2), rails, faults, notices
#   - prints at once when faults/notices/rails change (e.g. the 85 C shed or the 95 C buck cut-off)
#   - at the end: start / peak / last temperature, then releases the minimum
# Usage on the bench Pi:  ./pico.py run picohub_thermal_log.py 1500
import time
import board
import busio

DURATION = 1320          # 22 min: 2 min baseline without load + 15 min loaded + 5 min margin
SAMPLE_S = 10
MIN_VOLTS = 20
FALLBACK_ADDR = 0x60

i2c = busio.I2C(board.GP11, board.GP10, frequency=100000)
while not i2c.try_lock():
    pass

def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc

def u16(b, i):
    return b[i] | (b[i + 1] << 8)

def s16(b, i):
    v = u16(b, i)
    return v - 0x10000 if v & 0x8000 else v

def status(addr):
    try:
        i2c.writeto(addr, bytes([0x10]))
        b = bytearray(20)
        i2c.readfrom_into(addr, b)
    except OSError:
        return None
    if crc8(b[:19]) != b[19] or b[0] != 1:      # format 1 only (other modules answer 0x10 differently)
        return None
    return {"mv": u16(b, 2), "vin": u16(b, 6), "iin": u16(b, 8), "v5": u16(b, 10),
            "temp": s16(b, 12) / 10, "rails": b[14], "faults": b[16], "notices": b[17]}

def enumerate_x035(addr):
    # After a power loss the power manager is unassigned at 0x7F: give it an address again.
    try:
        b = bytearray(10)
        i2c.readfrom_into(0x7F, b)
        if b[8] == 0x07 and crc8(b[:9]) == b[9]:
            i2c.writeto(0x7F, bytes([0x1D, addr]))
            return True
    except OSError:
        pass
    return False

def find():
    for a in range(0x08, 0x78):
        if status(a) is not None:
            return a
    if enumerate_x035(FALLBACK_ADDR):
        return FALLBACK_ADDR
    return None

def set_min(addr, volts):
    try:
        i2c.writeto(addr, bytes([0x23, volts]))
        return True
    except OSError:
        return False

def rails_text(r):
    return "".join(n if r & m else "-" for n, m in (("A", 1), ("B", 2), ("J", 4), ("M", 8), ("K", 16)))

def line(t, s):
    return ("t=%4ds  %5.1f C  contract=%5dmV  vin=%5dmV  iin=%4dmA  v5=%4dmV  rails=%s  faults=0x%02X  notices=0x%02X"
            % (t, s["temp"], s["mv"], s["vin"], s["iin"], s["v5"], rails_text(s["rails"]), s["faults"], s["notices"]))

t0 = time.monotonic()
addr = find()
print("power manager at %s" % (hex(addr) if addr else "NOT FOUND - check the board / bus A"))
if addr and MIN_VOLTS:
    print("SET_MIN_INPUT_VOLTAGE %d: %s" % (MIN_VOLTS, "sent" if set_min(addr, MIN_VOLTS) else "write failed"))

t_start, t_peak, t_last = None, -99.0, None
last_key, next_sample, silent = None, 0, False
while time.monotonic() - t0 < DURATION:
    t = time.monotonic() - t0
    s = status(addr) if addr else None
    if s is None:
        if not silent:
            print("t=%4ds  power manager silent" % t)
            silent = True
        a = find()
        if a:
            addr = a
            print("t=%4ds  power manager back at 0x%02X" % (t, addr))
            if MIN_VOLTS:
                set_min(addr, MIN_VOLTS)     # an X035 restart forgets the minimum
            silent = False
        time.sleep(0.5)
        continue
    silent = False
    if t_start is None:
        t_start = s["temp"]
    t_peak = max(t_peak, s["temp"])
    t_last = s["temp"]
    key = (s["rails"], s["faults"], s["notices"], s["mv"])
    if key != last_key and last_key is not None:
        print(line(t, s) + "  <- CHANGE")
    last_key = key
    if t >= next_sample:
        print(line(t, s))
        next_sample += SAMPLE_S
    time.sleep(0.2)

if addr:
    set_min(addr, 0)
print("DONE  start %.1f C  peak %.1f C  last %.1f C  rise %.1f K  (minimum released)"
      % (t_start or 0, t_peak, t_last or 0, t_peak - (t_start or 0)))
