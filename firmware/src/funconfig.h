// SPDX-License-Identifier: MIT
// Copyright (c) 2026 noknok
#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

// noknok PicoHub (MOD-0011) power manager. VDD is 3.3 V (ME6203), so NOT the 5 V VDD
// setting of ch32fun's usbpd_sink example.
#define FUNCONF_ENABLE_HPE       1   // hardware prologue/epilogue for interrupts (fast USB-PD IRQ)
#define FUNCONF_SYSTICK_USE_HCLK 1   // SysTick counts at 48 MHz: millis() = CNT / 48000
#define FUNCONF_USE_DEBUGPRINTF  1   // status over the SWIO debug link (minichlink -T); only
                                     // printed while a debugger is attached, see LOG()
#define USBPD_MAX_CURRENT_10MA   300 // never request more than 3 A (board design limit)

#endif
