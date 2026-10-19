#!/bin/sh
# Persistent minichlink debug terminal for the MOD-0011 X035.
# Keys: printf 'a' >> /tmp/x035_in.txt    Output: /tmp/x035_out.log
cd "${BRINGUP_DIR:-/home/noknok/dev/ch32fun/noknok_picohub_bringup}" || exit 1   # build folder (this one), next to ch32fun
pkill -f "script -q -f -c ../minichlink/minichlink" 2>/dev/null
pkill -x tail 2>/dev/null
rm -f /tmp/x035_in.txt /tmp/x035_out.log
touch /tmp/x035_in.txt
setsid nohup sh -c "tail -f /tmp/x035_in.txt | script -q -f -c ../minichlink/minichlink\ -T /tmp/x035_out.log" >/dev/null 2>&1 </dev/null &
sleep 1.5
printf 'zzzzzzzzz\n' >> /tmp/x035_in.txt
