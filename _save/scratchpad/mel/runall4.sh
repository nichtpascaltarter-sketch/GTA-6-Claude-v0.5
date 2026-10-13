#!/bin/sh
# Final in-game verification with bin/nt_char3.exe: viewer shots (0.6 / 1.5 / 3 m, crowd, night, hands, glasses),
# then the camfade autoplay (a woman 0.75 m in front of the first-person camera, as in the lead's reference).
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd $SP
./memgate.sh 2400
EXE=bin/nt_char3.exe W=1280 H=720 TIMEOUT=3600 SHOTS="-261.6,1519.40,6.03,0,-2,10.0,c24_06 -261.6,1519.70,6.00,0,-3,10.0,c24_macro -239.2,1519.40,6.05,0,-3,10.0,c38_06 -277.6,1519.40,5.89,0,-2,10.0,c14_06 -260.0,1519.40,5.99,0,-2,10.0,c25_06_sunglasses -287.2,1519.40,5.91,0,-2,10.0,c8_06_aviators -272.8,1518.5,5.87,0,-3,10.0,c17_15 -288.8,1518.5,5.95,0,-3,10.0,c7_15 -284.0,1517.0,5.9,0,-4,10.0,c10_3 -286.4,1512.5,6.1,0,-5,10.0,crowd -261.6,1519.40,6.03,0,-2,22.0,c24_night -261.8,1519.5,5.25,0,-8,10.0,c24_hand" timeout 3700 ./mel/shoot.sh p3c --viewer characters --count 40 --clip 0 --settle 12 --quality 2 --clouds 0.2
echo "viewer done"
./memgate.sh 2400
mkdir -p /tmp/charcf
cd /home/user/GTA-6-Claude-v0.5
WINEPREFIX=/tmp/wine_char EXE=bin/nt_char3.exe TIMEOUT=3600 nice -n 10 tools/run.sh --width 960 --height 540 --play --autoplay camfade --renderevery 8 --shotdir "Z:\\tmp\\charcf\\" > /tmp/charcf/run.log 2>&1
echo "camfade exit $?"
for f in /tmp/charcf/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
ls /tmp/charcf/
echo "fp start"
cd $SP
./memgate.sh 2400
mkdir -p /tmp/charfp3
cd /home/user/GTA-6-Claude-v0.5
WINEPREFIX=/tmp/wine_char EXE=bin/nt_char3.exe TIMEOUT=6600 nice -n 10 tools/run.sh --width 960 --height 540 --play --firstperson --autoplay fpguns --renderevery 8 --shotdir "Z:\\tmp\\charfp3\\" > /tmp/charfp3/run.log 2>&1
echo "fp exit $?"
for f in /tmp/charfp3/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
ls /tmp/charfp3/
