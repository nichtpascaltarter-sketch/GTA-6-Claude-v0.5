#!/bin/sh
# After the snapshot build commits: first-person guns test, then the streetcar and metro end-to-end tests (one Wine
# instance at a time), all with the snapshot exe.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
until grep -q "COMMITTED\|BUILD FAILED" $SP/snap_fixed_run.log 2>/dev/null; do sleep 20; done
grep -q COMMITTED $SP/snap_fixed_run.log || { echo "snapshot build failed: nothing to verify"; exit 1; }
cp $REPO/bin/nt_snap.exe $SP/verify_test.exe
cd $REPO
LOG=/tmp/wine_lead/drive_c/users/root/AppData/Local/NeonTide/log.txt
rm -rf /tmp/fpw2; mkdir -p /tmp/fpw2
$SP/memgate.sh 2400
EXE=$SP/verify_test.exe WINEPREFIX=/tmp/wine_lead TIMEOUT=6000 nice -n 6 tools/run.sh --width 800 --height 450 --play --firstperson --autoplay fpguns --renderevery 8 --quality 1 --shotdir 'Z:\tmp\fpw2\' > /tmp/fpw2/run.txt 2>&1
echo "fpguns exit $?"; grep -E "fpguns|Unhandled" $LOG | tail -14
mkdir -p /tmp/ltransit; rm -f /tmp/ltransit/*
for mode in tram metro; do
  $SP/memgate.sh 2400
  EXE=$SP/verify_test.exe WINEPREFIX=/tmp/wine_lead TIMEOUT=7000 nice -n 6 tools/run.sh --width 640 --height 360 --play --autoplay $mode --autoduration 900 --autoevery 90 --renderevery 15 --transithour 11 --shotdir 'Z:\tmp\ltransit\' > /tmp/ltransit/run_$mode.txt 2>&1
  echo "$mode exit $?"; grep -E "Transit test \[$mode\]: (PASSED|FAILED|stuck|stage)|Unhandled" $LOG | tail -8
  cp $LOG /tmp/ltransit/log_$mode.txt
done
echo "VERIFY DONE"
