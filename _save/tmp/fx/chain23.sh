#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh
$G 2400; W=960 H=540 EXE=bin/nt_fx23.exe SHOTS="$(cat /tmp/fx/street_shots.txt)" TIMEOUT=2400 sh /tmp/fx/shoot.sh st2 --clouds 0.3 --settle 14 --quality 2
echo "st2 done" > /tmp/fx/chain23.state
$G 2400; W=960 H=540 EXE=bin/nt_fx23.exe SHOTS="1745,348,5.4,-90,-8,21.5,rain_street 3010,-300,3.8,0,-8,21.5,rain_downtown" TIMEOUT=2400 sh /tmp/fx/shoot.sh rn0 --rain 0.8 --clouds 0.9 --settle 14 --quality 2
echo "rn0 done" > /tmp/fx/chain23.state
