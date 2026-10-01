#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh
SH="-300,1519.35,6.15,0,-3,10.5,h0_front -299.4,1520.0,6.18,90,-4,10.5,h0_side -300,1520.65,6.2,180,-5,10.5,h0_back -298.4,1519.35,6.15,0,-3,17.8,h1_eve -296.8,1519.35,6.15,0,-3,10.5,h2_front"
$G 2600; W=640 H=360 EXE=bin/nt_fx16.exe SHOTS="$SH" TIMEOUT=2400 sh /tmp/fx/shoot.sh hc1 --viewer characters --count 3 --clip 0 --settle 12 --quality 2 --clouds 0.2
echo "hair done" > /tmp/fx/chain16.state
$G 2600; W=800 H=450 EXE=bin/nt_fx16.exe SHOTS="$(cat /tmp/fx/env_after.txt)" TIMEOUT=3000 sh /tmp/fx/shoot.sh env2 --clouds 0.45 --settle 14 --quality 2
echo "env done" > /tmp/fx/chain16.state
