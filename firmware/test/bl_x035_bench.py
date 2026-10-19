# SPDX-License-Identifier: MIT
# Copyright (c) 2026 noknok
"""MOD-0011 power manager - bootloader bench tests (DEV-124). Runs on the brain (CircuitPython).

The library part of this file is exec'd by a one-line driver, e.g. (on the Pi4):
    printf 'exec(open("/bl_x035_bench.py").read())\nsilence(120)\n' > t.py && python3 pico.py run t.py 300
Files it expects on the Pico (pico.py put): /module_flasher.py (brain-Pico software/, 1.17 or newer),
and the images the test names (e.g. /picohub_030.bin).

Tests (each prints PASS/FAIL lines):
  info()                 where is the power manager, which app version, is anything parked at 0x7E
  bl_info()              0xB0 -> bootloader 0xB1 (5 bytes, layout 3) + UID, then BOOT back to the app
  silence(secs)          USB-PD silence: sit in the bootloader for secs (no PD answers) while polling
                         0x7E every 100 ms; any gap = the board lost power (charger switched off).
                         Then BOOT and read the app's status: reset cause, contract, V_IN.
  update(path)           full app update from the app: 0xB0, ERASE, WRITE_CHUNK x N, VERIFY, BOOT;
                         prints the version before / after via 0xB1 and the time taken
  guard(v003_app, v003_stage1, good_app)
                         a CH32V003 app must be refused at VERIFY (error 9), a V003 stage-1 at
                         VERIFY_STAGE1 (error 8); then the good app is restored
  abort(path, frac)      start an update, stop after frac of the chunks (= brain reboot / power cut of
                         the brain mid-update): the X035 must stay parked at 0x7E, the board must keep
                         running (bus B still answers); then push path to recover
  recover(path)          if parked at 0x7E: push path and BOOT
  stage1(path, app)      stage-1 self-update over I2C (VERIFY_STAGE1 + BOOT -> stage-0 installs it),
                         version before/after, then the app is pushed again
"""
import time
import board
import busio
from module_flasher import ModuleFlasher, FlashError, BL_ADDR, crc32

BUS_A = (board.GP11, board.GP10)   # SCL, SDA: bus A (the X035 + J3/J4)
BUS_B = (board.GP21, board.GP20)   # bus B (J5/J6)
results = []


def check(name, ok, info=""):
    results.append((name, ok))
    print("%s  %s  %s" % ("PASS" if ok else "FAIL", name, info))


def summary():
    bad = [n for n, ok in results if not ok]
    print("SUMMARY: %d/%d passed%s" % (len(results) - len(bad), len(results),
                                       ("  FAILED: " + ", ".join(bad)) if bad else ""))


class Bus:
    """A locked-per-call wrapper so ModuleFlasher and the helpers share one busio.I2C."""
    def __init__(self, pins):
        self.i2c = busio.I2C(pins[0], pins[1], frequency=100000)

    def write(self, addr, data):
        while not self.i2c.try_lock():
            pass
        try:
            self.i2c.writeto(addr, bytes(data))
            return True
        except OSError:
            return False
        finally:
            self.i2c.unlock()

    def read(self, addr, n):
        buf = bytearray(n)
        while not self.i2c.try_lock():
            pass
        try:
            self.i2c.readfrom_into(addr, buf)
            return buf
        except OSError:
            return None
        finally:
            self.i2c.unlock()


A = Bus(BUS_A)
F = ModuleFlasher(A.i2c)


def crc8(d):
    c = 0
    for b in d:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 7) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def find_hub(timeout=6.0):
    """Address of the power manager (answers GET_STATUS with format 1 and a good CRC8), or None.
    Also handles a fresh enumeration at 0x7F (assigns 0x08)."""
    t0 = time.monotonic()
    while time.monotonic() - t0 < timeout:
        r = A.read(0x7F, 10)
        if r and r[8] == 0x07 and crc8(r[:9]) == r[9]:
            A.write(0x7F, [0x1D, 0x08])
            time.sleep(0.05)
        for a in range(0x08, 0x78):
            if A.write(a, [0x10]):
                b = A.read(a, 20)
                if b and b[0] == 1 and crc8(b[:19]) == b[19]:
                    return a
        time.sleep(0.2)
    return None


def app_version(addr):
    if not A.write(addr, [0xB1]):
        return None
    b = A.read(addr, 4)
    return tuple(b) if b else None


def status(addr):
    for _ in range(2):
        if not A.write(addr, [0x10]):
            continue
        b = A.read(addr, 20)
        if b and crc8(b[:19]) == b[19]:
            u = lambda i: b[i] | (b[i + 1] << 8)
            return {"pd": b[1], "mv": u(2), "ma": u(4), "vin": u(6), "iin": u(8), "v5": u(10),
                    "rails": b[14], "faults": b[16], "notices": b[17], "reset": b[18]}
    return None


def bus_b_alive():
    """True if anything answers on bus B (the board kept running), None if bus B is empty."""
    try:
        B = Bus(BUS_B)
    except Exception:
        return None
    found = None
    for a in list(range(0x08, 0x78)) + [0x7F]:
        if B.read(a, 1) is not None:
            found = a
            break
    B.i2c.deinit()
    return found


def info():
    a = find_hub()
    print("power manager at", hex(a) if a else None, "app", app_version(a) if a else None)
    print("status", status(a) if a else None)
    print("0x7E", F._read_status(), " bus B first device", bus_b_alive())
    return a


def enter_bl(addr):
    F.enter_bootloader(addr)
    F.wait_for_bootloader(timeout=3.0)


def bl_info():
    a = find_hub()
    check("power manager found", a is not None, hex(a) if a else "")
    if a is None:
        return
    before = app_version(a)
    enter_bl(a)
    v = F.get_version()
    uid = F.get_uid()
    check("bootloader 0xB1 = 5 bytes, layout 3", v is not None and len(v) == 5 and v[4] == 3, v)
    check("bootloader 0xB3 = 8-byte UID", uid is not None, uid)
    print("bus B while in the bootloader:", bus_b_alive())
    F.boot()
    time.sleep(1.0)
    a2 = find_hub()
    check("app back after BOOT (same address)", a2 == a, "%s -> %s, app %s" % (hex(a), a2 and hex(a2), before))
    s = status(a2) if a2 else None
    print("status after BOOT", s)


def silence(secs=120):
    a = find_hub()
    check("power manager found", a is not None)
    if a is None:
        return
    s0 = status(a)
    print("before: ", s0)
    enter_bl(a)
    t0 = time.monotonic()
    gaps, last_ok, n = [], t0, 0
    bus_b = bus_b_alive()
    while time.monotonic() - t0 < secs:
        st = F._read_status()
        now = time.monotonic()
        if st is None:
            if now - last_ok > 0.3:
                gaps.append(round(now - t0, 1))
        else:
            last_ok = now
        n += 1
        time.sleep(0.1)
    check("0x7E answered for %d s without a gap (board kept power)" % secs, not gaps,
          "gaps at %s" % gaps[:10] if gaps else "%d polls" % n)
    print("bus B in the bootloader:", bus_b)
    F.boot()
    time.sleep(1.5)
    a2 = find_hub()
    s1 = status(a2) if a2 else None
    print("after:  ", s1)
    check("app back at the same address, warm (reset cause != power-on)",
          a2 == a and s1 is not None and s1["reset"] != 0, s1 and s1["reset"])
    time.sleep(2.0)
    s2 = status(a2) if a2 else None
    print("2 s later:", s2)
    check("contract back above 5 V after the app's soft reset", s2 is not None and s2["pd"] == 2 and s2["mv"] > 5000,
          s2 and (s2["pd"], s2["mv"]))


def _push(img, from_addr=None, stop_after=None, stage1=False):
    """ERASE + chunks (+ VERIFY/VERIFY_STAGE1). Returns None or the FlashError text."""
    try:
        if from_addr is not None:
            enter_bl(from_addr)
        F.wait_for_bootloader()
        F.erase()
        n = (len(img) + 63) // 64
        for i in range(n):
            if stop_after is not None and i >= stop_after:
                return "stopped after %d of %d chunks" % (i, n)
            F.write_chunk(i * 64, img[i * 64:i * 64 + 64])
        if stage1:
            F.verify_stage1(len(img), crc32(img))
        else:
            F.verify(len(img), crc32(img))
        return None
    except FlashError as e:
        return str(e)


def recover(path):
    """If the X035 sits parked at 0x7E, push path and BOOT (what the brain's rescue does)."""
    st = F._read_status()
    if st is None:
        print("nothing parked at 0x7E")
        return
    print("parked at 0x7E, status", st, "- pushing", path)
    err = _push(open(path, "rb").read())
    check("parked X035 recovered with %s" % path, err is None, err or "")
    F.boot()
    time.sleep(1.5)
    check("app running after the recovery", find_hub() is not None)


def update(path):
    img = open(path, "rb").read()
    a = find_hub()
    check("power manager found", a is not None)
    if a is None:
        return
    before = app_version(a)
    t0 = time.monotonic()
    err = _push(img, from_addr=a)
    check("update %s (%d B) accepted" % (path, len(img)), err is None, err or "")
    F.boot()
    took = time.monotonic() - t0
    time.sleep(1.5)
    a2 = find_hub()
    after = app_version(a2) if a2 else None
    print("app before", before, "after", after, "address", hex(a), "->", a2 and hex(a2), "took %.1f s" % took)
    exp = tuple(img[0x200 + 12 + i] for i in (3, 2, 1, 0))   # app header version word, big end first
    check("0xB1 after = the image's version", after is not None and tuple(after) == exp, "%s vs %s" % (after, exp))
    print("status", status(a2) if a2 else None)


def guard(v003_app, v003_stage1, good_app):
    a = find_hub()
    check("power manager found", a is not None)
    if a is None:
        return
    enter_bl(a)
    err = _push(open(v003_app, "rb").read())
    check("CH32V003 app refused at VERIFY (error 9)", err is not None and "code 9" in err, err)
    err = _push(open(v003_stage1, "rb").read(), stage1=True)
    check("CH32V003 stage-1 refused at VERIFY_STAGE1 (error 8)", err is not None and "code 8" in err, err)
    check("still parked in the bootloader", F._read_status() is not None)
    err = _push(open(good_app, "rb").read())
    check("good app accepted", err is None, err or "")
    F.boot()
    time.sleep(1.5)
    check("app running again", find_hub() is not None)


def abort(path, frac=0.5):
    img = open(path, "rb").read()
    a = find_hub()
    check("power manager found", a is not None)
    if a is None:
        return
    n = (len(img) + 63) // 64
    err = _push(img, from_addr=a, stop_after=int(n * frac))
    print("interrupted:", err)
    st = F._read_status()
    check("parked at 0x7E after the interrupted update", st is not None, st)
    print("bus B while parked:", bus_b_alive())
    F.boot()                                          # no valid app: must refuse (error 6)
    time.sleep(0.3)
    st = F._read_status()
    check("BOOT with half an app refused, still parked (error 6)", st is not None and st[1] == 6, st)
    err = _push(img)
    check("recovered by pushing the app again", err is None, err or "")
    F.boot()
    time.sleep(1.5)
    check("app running again", find_hub() is not None)


def pull_loop(minutes, a_path="/picohub_030.bin", b_path="/picohub_031.bin"):
    """Power-pull run (acceptance: 50 pulls). Updates back and forth between two app images for
    `minutes` while a person (or a relay) cuts J1 at random moments. The Pico must be powered from
    its own USB (Pi) so it survives the cut. After every failed update it waits for the board to
    come back and classifies it: OLD app, NEW app, or PARKED at 0x7E (then recovered with the
    image it was writing, as the brain's rescue does). A board that does not come back within
    60 s is a BRICK = FAIL. Prints one line per update and a tally at the end."""
    imgs = [open(a_path, "rb").read(), open(b_path, "rb").read()]
    tally = {"ok": 0, "old": 0, "new": 0, "parked": 0, "brick": 0}
    t_end = time.monotonic() + minutes * 60
    k = 0
    while time.monotonic() < t_end:
        img = imgs[k % 2]
        want = tuple(img[0x200 + 12 + i] for i in (3, 2, 1, 0))
        a = find_hub(timeout=10)
        if a is None and F._read_status() is None:
            print("waiting for the board...")
            time.sleep(1)
            continue
        err = _push(img, from_addr=a) if a is not None else _push(img)
        if err is None:
            F.boot()
            time.sleep(1.5)
            tally["ok"] += 1
            k += 1
            print("#%d ok -> %s" % (k, want[1:]))
            continue
        print("#%d interrupted: %s" % (k + 1, err))
        t0, outcome = time.monotonic(), None
        while time.monotonic() - t0 < 60 and outcome is None:
            time.sleep(0.5)
            if F._read_status() is not None:
                outcome = "parked"
                e2 = _push(img)
                F.boot()
                print("   parked at 0x7E -> recovered:", e2 or "ok")
                time.sleep(1.5)
                break
            h = find_hub(timeout=4)
            if h is not None:
                v = app_version(h)
                outcome = "new" if v and tuple(v) == want else "old"
                print("   came back running app", v, "=", outcome, " status", status(h))
        tally[outcome or "brick"] += 1
        check("pull #%d: board came back (%s)" % (k + 1, outcome), outcome is not None)
        k += 1
    print("PULL TALLY:", tally)


def stage1(path, app):
    s1 = open(path, "rb").read()
    a = find_hub()
    check("power manager found", a is not None)
    if a is None:
        return
    enter_bl(a)
    before = F.get_version()
    err = _push(s1, stage1=True)
    check("VERIFY_STAGE1 accepted", err is None, err or "")
    F.boot()
    F.wait_for_bootloader_gone(timeout=2.0)
    F.wait_for_bootloader(timeout=5.0)
    after = F.get_version()
    print("stage-1 before", before, "after", after)
    exp = (s1[0x200 + 15], s1[0x200 + 14], s1[0x200 + 13], s1[0x200 + 12])   # header version word
    check("stage-1 version after = the image's", after is not None and tuple(after[:4]) == exp, "%s vs %s" % (after, exp))
    err = _push(open(app, "rb").read())
    check("app pushed after the stage-1 update", err is None, err or "")
    F.boot()
    time.sleep(1.5)
    check("app running again", find_hub() is not None)
