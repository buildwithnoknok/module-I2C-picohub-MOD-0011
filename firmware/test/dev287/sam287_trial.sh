#!/bin/bash
# SPDX-License-Identifier: MIT
# DEV-287 (Explorer Sam): $1 label, $2 driver. Full cut, wait for /rpc, driver, console 60 s, then /rpc uptime.
# PASS = the driver did not survive (no "STILL ALIVE"), uptime after <= time since the driver (it rebooted),
# exactly one USB port open in the 60 s console window after it (no loop) and /rpc answers, strikes 0.
cd ~/dev/pico; PLUG=192.168.1.104; IP=192.168.1.241
cut() { curl -s "http://$PLUG/rpc/Switch.Set?id=0&on=false" >/dev/null; echo raspberry | sudo -S -p "" /usr/sbin/uhubctl -l 2 -a off >/dev/null; sleep 6
        curl -s "http://$PLUG/rpc/Switch.Set?id=0&on=true" >/dev/null; echo raspberry | sudo -S -p "" /usr/sbin/uhubctl -l 2 -a on >/dev/null; }
up() { python3 rpc_call.py $IP mem 2>/dev/null | python3 -c "import sys,json
try: d=json.load(sys.stdin); d=d.get(\"result\",d); print(d[\"uptime\"], d[\"strikes\"])
except Exception: pass"; }
cut; for k in $(seq 1 30); do [ -n "$(up)" ] && break; sleep 5; done; echo "$1 before: up/strikes $(up)"
L=~/dev/pico/sam287/con_$1.log; rm -f $L
t0=$(date +%s); out=$(timeout 40 python3 pico.py run $2 15 2>&1); echo "$1 driver: $(echo "$out" | grep -E "PM at|ALIVE|SerialException" | tr "\n" " ")"
timeout 62 python3 sam285/sam285_conlog.py 60 $L
r=""; for k in $(seq 1 20); do r=$(up); [ -n "$r" ] && break; sleep 5; done
echo "TRIAL $1: port_opens=$(grep -c 'port open' $L) enum_lines=$(grep -c 'Enumerating noknok modules' $L) since_driver=$(( $(date +%s) - t0 ))s up/strikes_after=${r:-NONE}"
