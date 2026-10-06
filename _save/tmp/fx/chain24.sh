#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
while pgrep -f "OUT=bin/nt_fx24" >/dev/null || [ ! -f bin/nt_fx24.exe ]; do sleep 15; done
sleep 5
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh
SH="$(cat /tmp/fx/street_shots.txt) 3014,-290,3.9,-35,-4,14.0,storefront_day 1745,348,5.4,-90,18,22.0,windows_night 1745,348,5.4,-90,-6,21.0,street_dusk_dry 5462,955,1.9,0,-7,17.8,pond_beach"
$G 2400; W=960 H=540 EXE=bin/nt_fx24.exe SHOTS="$SH" TIMEOUT=3000 sh /tmp/fx/shoot.sh st3 --clouds 0.3 --settle 14 --quality 2
echo "st3 done" > /tmp/fx/chain24.state
$G 2400; W=960 H=540 EXE=bin/nt_fx24.exe SHOTS="1745,348,5.4,-90,-8,21.5,rain_street 3010,-300,3.8,0,-8,21.5,rain_downtown 3014,-290,3.9,-35,-4,21.5,rain_storefront" TIMEOUT=2400 sh /tmp/fx/shoot.sh rn1 --rain 0.8 --clouds 0.9 --settle 14 --quality 2
echo "rn1 done" > /tmp/fx/chain24.state
