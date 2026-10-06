#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
while ! grep -q "hair2 done" /tmp/fx/chain16b.state 2>/dev/null; do sleep 10; done
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh
SH="-290.4,1519.05,5.75,0,-10,10.5,c6_chest -289.75,1519.1,6.0,35,-6,10.5,c6_quarter -293,1511.5,6.2,0,-3,10.5,crowd_a -293,1511.5,6.2,0,-3,10.52,crowd_b"
$G 2600; W=960 H=540 EXE=bin/nt_fx17.exe SHOTS="$SH" TIMEOUT=2400 sh /tmp/fx/shoot.sh cl1 --viewer characters --count 10 --clip 1 --settle 12 --quality 2 --clouds 0.2
echo "cloth done" > /tmp/fx/chain17.state
$G 2600; W=640 H=360 EXE=bin/nt_fx17.exe SHOTS="5462,955,1.9,0,-7,17.8,probe17" TIMEOUT=2400 sh /tmp/fx/shoot.sh pd1 --clouds 0.45 --settle 14 --quality 2 --debugview 17
echo "probe done" > /tmp/fx/chain17.state
$G 2600; W=800 H=450 EXE=bin/nt_fx17.exe SHOTS="4600,250,16,90,-24,16.5,wake_a 4600,250,16,90,-24,16.52,wake_b" TIMEOUT=2400 sh /tmp/fx/shoot.sh wk1 --clouds 0.3 --settle 20 --quality 2 --fxdemo
echo "wake done" > /tmp/fx/chain17.state
