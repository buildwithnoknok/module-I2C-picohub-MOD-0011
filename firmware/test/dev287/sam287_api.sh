#!/bin/bash
# SPDX-License-Identifier: MIT
# DEV-287 (Explorer Sam): Sue's api5b.sh with fresh logs + diagnostics when the brain does not come back.
# Per trial: full cut, wait for /rpc, hub.reset_pico() (write + result read), console 40 s, poll /rpc 150 s.
cd ~/dev/pico; PLUG=192.168.1.104; IP=192.168.1.241
cut() { curl -s "http://$PLUG/rpc/Switch.Set?id=0&on=false" >/dev/null; echo raspberry | sudo -S -p "" /usr/sbin/uhubctl -l 2 -a off >/dev/null; sleep 6
        curl -s "http://$PLUG/rpc/Switch.Set?id=0&on=true" >/dev/null; echo raspberry | sudo -S -p "" /usr/sbin/uhubctl -l 2 -a on >/dev/null; }
up() { python3 rpc_call.py $IP mem 2>/dev/null | python3 -c "import sys,json
try: d=json.load(sys.stdin); d=d.get(\"result\",d); print(d[\"uptime\"], d[\"strikes\"])
except Exception: pass"; }
for i in $@; do
  L=~/dev/pico/sam287/con_$i.log; rm -f $L
  cut; for k in $(seq 1 30); do [ -n "$(up)" ] && break; sleep 5; done; echo "$i before: up/strikes $(up)"
  ran=""; for k in 1 2 3; do out=$(timeout 40 python3 pico.py run sam287/zz_sam287_api.py 15 2>&1); echo "$out" | grep -q "result" && { ran=1; break; }; sleep 5; done
  t0=$(date +%s); echo "$i driver: $(echo "$out" | grep -E "PM hub|result|ALIVE" | tr "\n" " ")"
  timeout 42 python3 sam285/sam285_conlog.py 40 $L
  first=""; while [ $(( $(date +%s) - t0 )) -lt 150 ]; do r=$(up); [ -n "$r" ] && { first="$(( $(date +%s) - t0 )) s, up/strikes $r"; break; }; sleep 5; done
  echo "TRIAL $i: usb_opens=$(grep -c "port open" $L 2>/dev/null) boots=$(grep -c 'cfg\] pins' $L 2>/dev/null) back_after=${first:-NEVER(150s)}"
  if [ -z "$first" ]; then echo "  DIAG lsusb: $(lsusb | grep -i 2e8a)"; echo "  DIAG by-id: $(ls /dev/serial/by-id/ 2>&1 | tr '\n' ' ')"; echo "  DIAG ping: $(ping -c 2 -W 1 $IP | grep -c ttl)"; fi
done
echo API DONE
