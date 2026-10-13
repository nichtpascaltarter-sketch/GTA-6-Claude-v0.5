#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
while pgrep -f "x86_64-w64-mingw32-g++" >/dev/null; do sleep 10; done
[ -f bin/nt_fx19.exe ] || { echo "no exe" > /tmp/fx/chain19.state; exit 1; }
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh
SH="$(cat /tmp/fx/env_after.txt) -300,1490,6.2,0,-15,18.6,apron_sunset"
$G 2600; W=800 H=450 EXE=bin/nt_fx19.exe SHOTS="$SH" TIMEOUT=3000 sh /tmp/fx/shoot.sh env3 --clouds 0.45 --settle 14 --quality 2
echo "env3 done" > /tmp/fx/chain19.state
SH2="-274.4,1519.3,6.15,0,-3,10.5,h16_front -284,1520.75,6.2,180,-6,10.5,h10_back -290.4,1519.05,5.75,0,-10,10.5,c6_chest"
$G 2600; W=640 H=360 EXE=bin/nt_fx19.exe SHOTS="$SH2" TIMEOUT=2400 sh /tmp/fx/shoot.sh hc3 --viewer characters --count 17 --clip 0 --settle 12 --quality 2 --clouds 0.2
echo "hc3 done" > /tmp/fx/chain19.state
