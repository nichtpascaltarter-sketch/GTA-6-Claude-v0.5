#!/bin/sh
# After lead_v7: carried props at tour stop 8 (downtown rain night: umbrellas open in the rain) on the lead_v7 build
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -qE "fpdrive exit|build failed" $SP/lead_v7.log 2>/dev/null; do sleep 30; done
grep -q "build failed" $SP/lead_v7.log && { echo "no build"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
LOGF=/tmp/wine_fp2/drive_c/users/root/AppData/Local/NeonTide/log.txt
for st in 8; do
  D=/tmp/v8_stop$st; rm -rf $D; mkdir -p $D
  sh $SP/memgate.sh 2400 >/dev/null 2>&1
  EXE=$SP/lead_v7_run.exe WINEPREFIX=/tmp/wine_fp2 TIMEOUT=4000 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart $st --tourcount 1 --renderevery 6 --quality 1 --shotdir "Z:\\tmp\\v8_stop$st\\" > $D/run.txt 2>&1
  echo "stop$st exit $?"
  cp $LOGF $D/log.txt 2>/dev/null
  grep -E "tour shot|Unhandled" $D/log.txt | tail -2 | cut -c1-200
done
