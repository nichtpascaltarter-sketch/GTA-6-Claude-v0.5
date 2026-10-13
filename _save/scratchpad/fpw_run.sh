#!/bin/sh
# Snapshot-build the working tree, then run the first-person guns test with that exe (screenshots in /tmp/fpw).
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
if [ "${NOBUILD:-0}" != 1 ]; then
  rm -f $SP/snap_tree_ok
  TRIES=${TRIES:-3} sh $SP/snap_loop.sh 2>&1 | tail -2
  [ -s $SP/snap_tree_ok ] || { echo "snapshot build failed"; exit 1; }
fi
cp bin/nt_snap.exe $SP/fpgun_test.exe
rm -f /tmp/fpw/*; mkdir -p /tmp/fpw
$SP/memgate.sh 2400
EXE=$SP/fpgun_test.exe WINEPREFIX=/tmp/wine_lead TIMEOUT=6000 nice -n 6 tools/run.sh --width ${W:-800} --height ${H:-450} --play --firstperson --autoplay fpguns --renderevery ${RE:-8} --quality 1 --shotdir 'Z:\tmp\fpw\' > /tmp/fpw/run.txt 2>&1
echo "run exit $?"
grep -E "fpguns|Unhandled" /tmp/wine_lead/drive_c/users/root/AppData/Local/NeonTide/log.txt | tail -14
