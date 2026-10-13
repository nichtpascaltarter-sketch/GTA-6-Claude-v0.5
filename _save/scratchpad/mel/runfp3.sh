#!/bin/sh
# First-person gun run (12 steps) on bin/nt_char3.exe at 960x540, rendering every 16th frame to fit the loaded machine.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd $SP
./memgate.sh 2400
mkdir -p /tmp/charfp3
cd /home/user/GTA-6-Claude-v0.5
WINEPREFIX=/tmp/wine_char EXE=bin/nt_char3.exe TIMEOUT=6000 nice -n 10 tools/run.sh --width 960 --height 540 --play --firstperson --autoplay fpguns --renderevery 16 --shotdir "Z:\\tmp\\charfp3\\" > /tmp/charfp3/run.log 2>&1
echo "fp exit $?"
for f in /tmp/charfp3/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
ls /tmp/charfp3/
