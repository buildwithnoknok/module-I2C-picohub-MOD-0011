# SPDX-License-Identifier: MIT
# noknok PicoHub (MOD-0011) power manager firmware - build on the Pi4 bench host.
#   make            picohub.bin (the app, linked at 0x1400 = the OTA payload)
#                   + picohub_swd.bin (stage-0 + stage-1 + app + metadata = the factory/bench SWD image)
#   make swd        flash picohub_swd.bin over SWD (J8, WCH-LinkE) - J1 on a plain 5 V source!
#
# The bootloader (stage-0/stage-1, CH32X035 port, layout 3) is built and released in the repo
# module-I2C-bootloader (firmware/bin/noknok_stage0_x035.bin, noknok_stage1_x035.bin); the SWD
# image is assembled by that repo's mk_full.py, the same tool as for the CH32V003 modules.
# BOOTLOADER = that repo's firmware/ folder (default: the sibling clone in noknok\repos).
all : picohub.bin picohub_swd.bin   # first target = the default goal (before ch32fun.mk adds its own)

TARGET     := picohub
TARGET_MCU := CH32X035
TARGET_MCU_PACKAGE := CH32X035G8U6

# Linked at the noknok bootloader app base (0x1400), see app.ld.
LINKER_SCRIPT := app.ld

# ch32fun checkout (default = the noknok bench host)
# override: make CH32FUN_DIR=/path/to/ch32fun
CH32FUN_DIR ?= /home/noknok/dev/ch32fun
BOOTLOADER  ?= ../../../module-I2C-bootloader/firmware

include $(CH32FUN_DIR)/ch32fun/ch32fun.mk

STAGES := $(BOOTLOADER)/bin/noknok_stage0_x035.bin $(BOOTLOADER)/bin/noknok_stage1_x035.bin

picohub_swd.bin : $(TARGET).bin $(STAGES) $(BOOTLOADER)/stage0/test/mk_full.py
	python3 $(BOOTLOADER)/stage0/test/mk_full.py --chip x035 $(STAGES) $(TARGET).bin $@

swd  : picohub_swd.bin
	$(CH32FUN_DIR)/minichlink/minichlink -w picohub_swd.bin flash -b

clean : cv_clean
	rm -f picohub_swd.bin
