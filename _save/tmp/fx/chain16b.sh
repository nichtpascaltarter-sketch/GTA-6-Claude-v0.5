#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
while ! grep -q "env done" /tmp/fx/chain16.state 2>/dev/null; do sleep 10; done
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh
SH="-284,1519.3,6.15,0,-3,10.5,h10_front -284,1520.75,6.2,180,-6,10.5,h10_back -283.35,1520,6.1,90,-3,10.5,h10_side -284,1519.3,6.15,0,-2,18.4,h10_eve -287.2,1520.75,6.2,180,-6,10.5,h8_back -274.4,1519.3,6.15,0,-3,10.5,h16_front"
$G 2600; W=640 H=360 EXE=bin/nt_fx16.exe SHOTS="$SH" TIMEOUT=2400 sh /tmp/fx/shoot.sh hc2 --viewer characters --count 17 --clip 0 --settle 12 --quality 2 --clouds 0.2
echo "hair2 done" > /tmp/fx/chain16b.state
