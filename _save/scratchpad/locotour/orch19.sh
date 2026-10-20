#!/bin/sh
# detached: after orch18 (st7 locostairs), one locotour run (tour stop 2) of the same st7 build - the current state of
# the locomotion tour with all of the animation work so far; log -> log_st7t.txt, shots deleted, exe copy deleted
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
until grep -q 'built st7\|no exe' $T/orch18.log 2>/dev/null; do sleep 20; done
grep -q 'built st7' $T/orch18.log || { echo "no st7 exe"; exit 1; }
[ -f $T/st7/bin/loco_st7t.exe ] || cp $T/st7/bin/loco_st7.exe $T/st7/bin/loco_st7t.exe
echo "copied $(date +%T)"
until grep -q 'orch18 done' $T/orch18.log 2>/dev/null; do sleep 30; done
cd $T/st7 || exit 1
mkdir -p $T/shots_st7t
export WINEPREFIX=/tmp/wine_anim
LOG=/tmp/wine_anim/drive_c/users/root/AppData/Local/NeonTide/log.txt
D="Z:$(echo $T/shots_st7t/ | sed 's|/|\\|g')"
EXE=bin/loco_st7t.exe TIMEOUT=4800 nice -n 6 sh tools/run.sh --width 960 --height 540 --play --autoplay locotour --renderevery 6 --quality 1 --shotdir "$D" > $T/run_st7t.txt 2>&1
echo "run st7t exit $?"
cp $LOG $T/log_st7t.txt
grep "locotour" $T/log_st7t.txt | grep -v "locotour shot \|big slide" | tail -30
rm -f $T/st7/bin/loco_st7t.exe $T/shots_st7t/*.bmp
echo "orch19 done $(date +%T)"
