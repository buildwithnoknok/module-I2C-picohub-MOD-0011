// SPDX-License-Identifier: MIT
// Copyright (c) 2026 noknok
#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

// Bring-up firmware only: default X035 clock (HSI 48 MHz), printf over the
// SWIO debug link (read it with `minichlink -T`). The printf has a timeout,
// so the firmware also runs with no debugger attached.
#define FUNCONF_USE_DEBUGPRINTF 1

#endif
