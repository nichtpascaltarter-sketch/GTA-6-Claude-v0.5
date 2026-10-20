#!/bin/sh
# QUICK build of /tmp/leadtree4, then: FP melee guard, magazine-in-hand reload steps, last UI screens
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /tmp/leadtree4 || exit 1
sh $SP/memgate.sh 3000 >/dev/null 2>&1
QUICK=1 OUT=bin/lead_v3.exe ./build.sh > $SP/lead_v3_build.log 2>&1 || { echo "build failed"; tail -5 $SP/lead_v3_build.log; exit 1; }
cp bin/lead_v3.exe $SP/lead_v3_run.exe
cd /home/user/GTA-6-Claude-v0.5
LOGF=/tmp/wine_fp2/drive_c/users/root/AppData/Local/NeonTide/log.txt
for s in melee mag ui; do
  D=/tmp/v3_$s; rm -rf $D; mkdir -p $D
  sh $SP/memgate.sh 2400 >/dev/null 2>&1
  case $s in
    melee) ARGS="--width 800 --height 450 --play --firstperson --autoplay melee --autoduration 11 --autoevery 0.8 --renderevery 3 --quality 1";;
    mag)   ARGS="--width 800 --height 450 --play --firstperson --autoplay fpguns --tourstart 16 --tourcount 2 --renderevery 6 --quality 1";;
    ui)    ARGS="--width 1280 --height 720 --play --autoplay uishots --tourstart 12 --tourcount 3 --renderevery 8 --quality 1";;
  esac
  EXE=$SP/lead_v3_run.exe WINEPREFIX=/tmp/wine_fp2 TIMEOUT=4500 nice -n 6 tools/run.sh $ARGS --shotdir "Z:\\tmp\\v3_$s\\" > $D/run.txt 2>&1
  echo "$s exit $?"
  cp $LOGF $D/log.txt 2>/dev/null
  grep -E "autoplay|Unhandled" $D/log.txt | tail -8 | cut -c1-200
done
