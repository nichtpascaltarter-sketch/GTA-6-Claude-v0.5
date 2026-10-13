#!/bin/sh
# First-person gun run (all 12 steps) on bin/nt_char2.exe at 960x540 (hands are unchanged since nt_char).
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd $SP
./memgate.sh 2400
mkdir -p /tmp/charfp2
cd /home/user/GTA-6-Claude-v0.5
WINEPREFIX=/tmp/wine_char EXE=bin/nt_char2.exe TIMEOUT=6600 nice -n 10 tools/run.sh --width 960 --height 540 --play --firstperson --autoplay fpguns --renderevery 8 --shotdir "Z:\\tmp\\charfp2\\" > /tmp/charfp2/run.log 2>&1
echo "fp exit $?"
for f in /tmp/charfp2/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
ls /tmp/charfp2/
