#!/bin/sh
# Free-camera verification shots: shots.sh EXE OUTDIR "x,y,z,yaw,pitch,hour,name" ...
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
EXE=$1; OUT=$2; shift 2
mkdir -p $OUT
ARGS=""
for s in "$@"; do ARGS="$ARGS --shot $s"; done
cd /home/user/GTA-6-Claude-v0.5
sh $SP/memgate.sh 2400
WOUT=$(echo "$OUT" | sed 's#/#\\\\#g')
EXE=$EXE WINEPREFIX=/tmp/wine_places TIMEOUT=${TIMEOUT:-3000} nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 $ARGS --shotdir "Z:${WOUT}\\" > $OUT/run.log 2>&1
echo "wine exit $?" >> $OUT/run.log
for f in $OUT/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "$f"; done
cp /tmp/wine_places/drive_c/users/root/AppData/Local/NeonTide/log.txt $OUT/game_log.txt 2>/dev/null
ls $OUT
