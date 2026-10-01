#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh
$G 2600; W=800 H=450 EXE=bin/nt_fx15.exe SHOTS="$(cat /tmp/fx/env_after.txt)" sh /tmp/fx/shoot.sh env1 --clouds 0.45 --settle 14 --quality 2
$G 2600; W=800 H=450 EXE=bin/nt_fx14.exe SHOTS="5462,955,1.9,0,-7,17.8,beach_shore 5458,945,1.9,-95,-10,17.8,beach_sea2 5466,950,1.8,90,3,18.6,beach_west" sh /tmp/fx/shoot.sh env0b --clouds 0.45 --settle 14 --quality 2
echo finished > /tmp/fx/env_chain.done
