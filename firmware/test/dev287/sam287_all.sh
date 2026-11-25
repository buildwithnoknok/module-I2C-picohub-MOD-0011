#!/bin/bash
# SPDX-License-Identifier: MIT
# DEV-287 bench (Explorer Sam): PM 0.4.1. 5x write-only RESET_PICO, 2x RESET_PICO + immediate GET_STATUS,
# then 5x hub.reset_pico() (write + result read) via sam287_api.sh.
cd ~/dev/pico
for i in 1 2 3 4 5; do bash sam287/sam287_trial.sh w$i sam287/zz_sam287_w.py; done
for i in 1 2; do bash sam287/sam287_trial.sh b$i sam287/zz_sam287_b.py; done
bash sam287/sam287_api.sh a1 a2 a3 a4 a5
echo ALL DONE
