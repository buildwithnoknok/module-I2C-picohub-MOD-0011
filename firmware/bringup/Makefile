# SPDX-License-Identifier: MIT
# MOD-0011 (noknok PicoHub) bring-up firmware - build on the Pi4 bench host.
all: flash

TARGET     := bringup
TARGET_MCU := CH32X035
TARGET_MCU_PACKAGE := CH32X035G8U6

# ch32fun checkout (default = the noknok bench host)
# override: make CH32FUN_DIR=/path/to/ch32fun
CH32FUN_DIR ?= /home/noknok/dev/ch32fun

include $(CH32FUN_DIR)/ch32fun/ch32fun.mk

flash:  cv_flash
clean:  cv_clean
