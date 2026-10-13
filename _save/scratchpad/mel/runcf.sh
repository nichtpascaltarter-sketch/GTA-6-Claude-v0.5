#!/bin/sh
# camfade autoplay alone on bin/nt_char3.exe (the lead's reference woman 0.75 m in front of the first-person camera)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd $SP
./memgate.sh 2400
mkdir -p /tmp/charcf2
cd /home/user/GTA-6-Claude-v0.5
WINEPREFIX=/tmp/wine_char EXE=bin/nt_char3.exe TIMEOUT=4200 nice -n 10 tools/run.sh --width 960 --height 540 --play --autoplay camfade --renderevery 8 --shotdir "Z:\\tmp\\charcf2\\" > /tmp/charcf2/run.log 2>&1
echo "camfade exit $?"
for f in /tmp/charcf2/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
ls /tmp/charcf2/
