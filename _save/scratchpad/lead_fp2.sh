#!/bin/sh
# Wait for the QUICK exe, then: first-person reload steps (fpguns 9..15) and first-person melee (one Wine run at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -q "rc=" $SP/lead_q_build.log 2>/dev/null; do sleep 20; done
grep -q "rc=0" $SP/lead_q_build.log || { echo "QUICK build failed"; exit 1; }
cp /tmp/leadtree3/bin/lead_q.exe $SP/lead_q_run.exe
cd /home/user/GTA-6-Claude-v0.5
LOGF=/tmp/wine_fp2/drive_c/users/root/AppData/Local/NeonTide/log.txt
for s in reload melee; do
  D=/tmp/fp2_$s; rm -rf $D; mkdir -p $D
  sh $SP/memgate.sh 2400 >/dev/null 2>&1
  case $s in
    reload) ARGS="--width 800 --height 450 --play --firstperson --autoplay fpguns --tourstart 9 --tourcount 7 --renderevery 6 --quality 1";;
    melee)  ARGS="--width 800 --height 450 --play --firstperson --autoplay melee --autoduration 13 --autoevery 1.2 --renderevery 3 --quality 1";;
  esac
  for attempt in 1 2; do
    EXE=$SP/lead_q_run.exe WINEPREFIX=/tmp/wine_fp2 TIMEOUT=5000 nice -n 6 tools/run.sh $ARGS --shotdir "Z:\\tmp\\fp2_$s\\" > $D/run.txt 2>&1
    rc=$?
    [ -f $LOGF ] && grep -q "autoplay" $LOGF && break
    echo "$s attempt $attempt exit $rc (retrying: prefix init)"
  done
  echo "$s exit $rc"
  cp $LOGF $D/log.txt 2>/dev/null
  grep -E "autoplay|Unhandled" $D/log.txt | tail -12 | cut -c1-200
done
