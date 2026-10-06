#!/bin/sh
# transit agent: static --shot views (no gameplay) from a list file: one "x,y,z,yaw,pitch,hour,name" per line
cd /home/user/GTA-6-Claude-v0.5
OUT=/tmp/transit/shots
mkdir -p $OUT
rm -f $OUT/*.bmp
ARGS=""
while read -r l; do [ -n "$l" ] && ARGS="$ARGS --shot $l"; done < ${1:-/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/tr_shots.txt}
WINEPREFIX=/tmp/wine_transit EXE=bin/nt_transit.exe TIMEOUT=${TO:-1500} nice -n 10 tools/run.sh --width ${W:-800} --height ${H:-450} --settle ${SETTLE:-24} $ARGS --shotdir 'Z:\tmp\transit\shots\' > $OUT/run.txt 2>&1
echo "exit $?" >> $OUT/run.txt
for f in $OUT/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "$f"; done
ls $OUT
