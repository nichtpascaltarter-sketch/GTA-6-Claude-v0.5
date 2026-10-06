#!/bin/sh
cd /home/user/GTA-6-Claude-v0.5
G=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh
SH="1745,348,5.4,-90,-35,10.5,asph_down 3010,-300,3.8,0,-12,14.0,asph_downtown 3014,-290,3.9,-35,-4,14.0,storefront_day 1745,348,5.4,-90,-6,21.0,street_dusk_dry"
$G 2400; W=960 H=540 EXE=bin/nt_fx25.exe SHOTS="$SH" TIMEOUT=3000 sh /tmp/fx/shoot.sh st4 --clouds 0.3 --settle 14 --quality 2
echo "st4 done" > /tmp/fx/chain25.state
$G 2400; W=960 H=540 EXE=bin/nt_fx25.exe SHOTS="3014,-290,3.9,-35,-4,14.0,ssr13" TIMEOUT=2400 sh /tmp/fx/shoot.sh st5 --clouds 0.3 --settle 10 --quality 2 --debugview 13
echo "st5 done" > /tmp/fx/chain25.state
