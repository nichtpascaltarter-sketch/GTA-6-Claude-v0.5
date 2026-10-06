#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
while ! grep -q "hc3 done" /tmp/fx/chain19.state 2>/dev/null; do sleep 10; done
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh
SH="-300,1490,6.2,0,-15,18.0,apron_18 -300,1490,6.2,0,-15,18.3,apron_183 -300,1490,6.2,-60,-12,17.9,apron_side"
$G 2600; W=800 H=450 EXE=bin/nt_fx19.exe SHOTS="$SH" TIMEOUT=2400 sh /tmp/fx/shoot.sh lp1 --clouds 0.3 --settle 14 --quality 2
echo "lp1 done" > /tmp/fx/chain20.state
