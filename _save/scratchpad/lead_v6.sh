#!/bin/sh
# QUICK build of /tmp/leadtree6 (carried props), then tour stops 0 (Calle Luna morning) and 8 (downtown rain night)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /tmp/leadtree6 || exit 1
sh $SP/memgate.sh 3000 >/dev/null 2>&1
QUICK=1 OUT=bin/lead_v6.exe ./build.sh > $SP/lead_v6_build.log 2>&1 || { echo "build failed"; grep -E "error" $SP/lead_v6_build.log | head; exit 1; }
cp bin/lead_v6.exe $SP/lead_v6_run.exe
until grep -q "fpdrive exit" $SP/lead_v5.log 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
LOGF=/tmp/wine_fp2/drive_c/users/root/AppData/Local/NeonTide/log.txt
for st in 0 8; do
  D=/tmp/v6_stop$st; rm -rf $D; mkdir -p $D
  sh $SP/memgate.sh 2400 >/dev/null 2>&1
  EXE=$SP/lead_v6_run.exe WINEPREFIX=/tmp/wine_fp2 TIMEOUT=4000 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart $st --tourcount 1 --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\v6_stop$st\\" > $D/run.txt 2>&1
  echo "stop$st exit $?"
  cp $LOGF $D/log.txt 2>/dev/null
  grep -E "tour shot|Unhandled" $D/log.txt | tail -2 | cut -c1-200
done
