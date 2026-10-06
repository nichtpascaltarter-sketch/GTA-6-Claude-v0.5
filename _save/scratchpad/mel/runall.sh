#!/bin/sh
# In-game verification after a build: viewer shots (faces, brows, beard, hands, crowd, night), then the first-person gun run.
cd /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
./memgate.sh 2400
W=1280 H=720 TIMEOUT=3000 SHOTS="-261.6,1519.40,6.03,0,-2,10.0,c24_06 -261.6,1519.70,6.00,0,-3,10.0,c24_macro -261.6,1519.72,5.95,0,-1,10.0,c24_mouth -239.2,1519.40,6.05,0,-3,10.0,c38_06 -290.4,1519.40,5.96,0,-2,10.0,c6_06 -277.6,1519.40,5.89,0,-2,10.0,c14_06 -288.8,1518.5,5.95,0,-3,10.0,c7_15 -284.0,1517.0,5.9,0,-4,10.0,c10_3 -286.4,1512.5,6.1,0,-5,10.0,crowd -261.6,1519.40,6.03,0,-2,22.0,c24_night -289.0,1519.55,5.2,0,-8,10.0,c7_hand -261.8,1519.5,5.25,0,-8,10.0,c24_hand" timeout 3100 ./mel/shoot.sh p3b --viewer characters --count 40 --clip 0 --settle 12 --quality 2 --clouds 0.2
echo "viewer done"
./memgate.sh 2400
mkdir -p /tmp/charfp
cd /home/user/GTA-6-Claude-v0.5
WINEPREFIX=/tmp/wine_char EXE=bin/nt_char.exe TIMEOUT=3000 nice -n 10 tools/run.sh --width 1280 --height 720 --play --firstperson --autoplay fpguns --renderevery 8 --shotdir "Z:\\tmp\\charfp\\" > /tmp/charfp/run.log 2>&1
echo "fp exit $?"
for f in /tmp/charfp/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
ls /tmp/charfp/
