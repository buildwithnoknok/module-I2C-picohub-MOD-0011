# SPDX-License-Identifier: MIT
# noknok PicoHub (MOD-0011) power manager firmware - build on the Pi4 bench host.
#   make            build picohub.bin (app, linked at 0x1400) + picohub_swd.bin (stub + app)
#   make swd        flash picohub_swd.bin over SWD (J8, WCH-LinkE) - bench/factory until DEV-124
all : picohub_swd.bin   # first target = the default goal (before ch32fun.mk adds its own)

TARGET     := picohub
TARGET_MCU := CH32X035
TARGET_MCU_PACKAGE := CH32X035G8U6

# Linked at the noknok bootloader app base (0x1400), see app.ld.
LINKER_SCRIPT := app.ld

# ch32fun checkout (default = the noknok bench host)
# override: make CH32FUN_DIR=/path/to/ch32fun
CH32FUN_DIR ?= /home/noknok/dev/ch32fun

include $(CH32FUN_DIR)/ch32fun/ch32fun.mk

picohub_swd.bin : $(TARGET).bin ../tools/mk_swd_image.py
	python3 ../tools/mk_swd_image.py $(TARGET).bin $@

swd  : picohub_swd.bin
	$(CH32FUN_DIR)/minichlink/minichlink -w picohub_swd.bin flash -b

clean : cv_clean
	rm -f picohub_swd.bin
