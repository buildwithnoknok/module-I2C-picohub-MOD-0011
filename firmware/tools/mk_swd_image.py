# SPDX-License-Identifier: MIT
# Copyright (c) 2026 noknok
"""Build the SWD image for the MOD-0011 power manager: a jump stub at 0x0000 + the app at 0x1400.

The app is linked at the noknok bootloader's app base (0x1400, ADR-004). Until the X035
bootloader port (DEV-124) exists, the first word of flash is a single RISC-V instruction
`jal x0, 0x1400` (a jump to the app's vector table), and the gap up to 0x1400 is erased
flash (0xFF). The X035 factory bootloader starts at 0x0000 as usual.

Usage: python3 mk_swd_image.py picohub.bin picohub_swd.bin
"""
import sys

APP_BASE = 0x1400
APP_MAX = 0xE000   # app region 0x1400..0xF3FF, see app.ld


def jal_x0(offset):
    """Encode `jal x0, offset` (J-type), offset relative to the instruction at 0x0000."""
    assert offset % 2 == 0 and -(1 << 20) <= offset < (1 << 20)
    imm = offset & 0x1FFFFF
    word = (((imm >> 20) & 1) << 31) | (((imm >> 1) & 0x3FF) << 21) | \
           (((imm >> 11) & 1) << 20) | (((imm >> 12) & 0xFF) << 12) | (0 << 7) | 0x6F
    return word.to_bytes(4, "little")


def main():
    app = open(sys.argv[1], "rb").read()
    if len(app) > APP_MAX:
        sys.exit("app is %d bytes, larger than the %d byte app region" % (len(app), APP_MAX))
    stub = jal_x0(APP_BASE)
    image = stub + b"\xff" * (APP_BASE - len(stub)) + app
    open(sys.argv[2], "wb").write(image)
    print("%s: stub %s + app %d bytes at 0x%04X = %d bytes" % (sys.argv[2], stub.hex(), len(app), APP_BASE, len(image)))


if __name__ == "__main__":
    main()
