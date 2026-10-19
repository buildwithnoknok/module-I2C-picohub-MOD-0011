#!/bin/sh
# SPDX-License-Identifier: MIT
# Persistent minichlink debug terminal for the MOD-0011 X035 (runs on the Pi4).
# Keys: printf 's' >> /tmp/x035_in.txt    Output: /tmp/x035_out.log
# Starting it resets the X035 (a debugger reset, not a power-on: the buck is not touched).
cd "${MINICHLINK_DIR:-/home/noknok/dev/ch32fun/minichlink}" || exit 1   # folder that holds ./minichlink
pkill -f "script -q -f -c ./minichlin[k]" 2>/dev/null
pkill -x tail 2>/dev/null
rm -f /tmp/x035_in.txt /tmp/x035_out.log
touch /tmp/x035_in.txt
setsid nohup sh -c "tail -f /tmp/x035_in.txt | script -q -f -c ./minichlink\ -T /tmp/x035_out.log" >/dev/null 2>&1 </dev/null &
sleep 1.5
printf 'zzzzzzzzz\n' >> /tmp/x035_in.txt
