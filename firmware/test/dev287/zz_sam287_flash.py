# SPDX-License-Identifier: MIT
# DEV-287 bench driver (Sam): push IMG (0.4.1) to the PicoHub power manager over I2C, BOOT, re-assign.
IMG = "/picohub_287.bin"
exec(open("/bl_x035_bench.py").read())
img = open(IMG, "rb").read()
a = find_hub()
check("power manager found before", a is not None, a and hex(a))
print("before: addr", a and hex(a), "app", app_version(a))
t0 = time.monotonic()
err = _push(img, from_addr=a)
check("push %s (%d B, crc %08x) accepted" % (IMG, len(img), crc32(img)), err is None, err or "")
F.boot()
time.sleep(1.5)
a2 = find_hub()
after = app_version(a2) if a2 else None
check("ASSIGN -> app answers, 0xB1 = 0.4.1", after is not None and tuple(after)[1:] == (0, 4, 1),
      "%s at %s, took %.1f s" % (after, a2 and hex(a2), time.monotonic() - t0))
print("status", status(a2))
summary()
