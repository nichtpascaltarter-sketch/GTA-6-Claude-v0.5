#!/bin/sh
# Lead verification queue on one exe: camera fade scene, first-person guns, tram and metro rides (one Wine run at a time).
# usage: lead_verify2.sh EXE [steps...]   steps: camfade fpguns tram metro (default all)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
EXE=$1; shift
STEPS=${*:-camfade fpguns tram metro}
cp "$EXE" $SP/verify2.exe
cd /home/user/GTA-6-Claude-v0.5
LOGF=/tmp/wine_lead/drive_c/users/root/AppData/Local/NeonTide/log.txt
for s in $STEPS; do
  D=/tmp/v2_$s; rm -rf $D; mkdir -p $D
  sh $SP/memgate.sh 2400 >/dev/null 2>&1
  case $s in
    camfade) ARGS="--width 960 --height 540 --play --autoplay camfade --renderevery 4 --quality 1"; TO=3000;;
    fpguns)  ARGS="--width 800 --height 450 --play --firstperson --autoplay fpguns --renderevery 8 --quality 1"; TO=6000;;
    tram|metro) ARGS="--width 640 --height 360 --play --autoplay $s --autoduration 900 --autoevery 90 --renderevery 15 --transithour 11"; TO=7000;;
  esac
  EXE=$SP/verify2.exe WINEPREFIX=/tmp/wine_lead TIMEOUT=$TO nice -n 6 tools/run.sh $ARGS --shotdir "Z:\\tmp\\v2_$s\\" > $D/run.txt 2>&1
  echo "$s exit $?"
  cp $LOGF $D/log.txt 2>/dev/null
  grep -E "autoplay|Unhandled|transit test|FAIL|PASS" $D/log.txt | tail -8 | cut -c1-220
done
