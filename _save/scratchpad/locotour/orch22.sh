#!/bin/sh
# detached: after 03:55 (main's hold), st7b locostairs with walker 180 traced (stairSeen / rootRatio in the trace); stopped
# 40 s after its 'bigslide 180' line (all that is needed); then one locotour run on st7's copied exe. One game at a time.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
LOG=/tmp/wine_anim/drive_c/users/root/AppData/Local/NeonTide/log.txt
echo "start $(date +%T)"
until [ "$(date +%H%M)" -ge 0355 ] && [ "$(date +%H%M)" -lt 1200 ]; do sleep 20; done
echo "launching st7b $(date +%T)"
[ -f $LOG ] && mv $LOG $LOG.prev   # (so the wait below reads this run's log only)
sh $T/run_st.sh st7b &
RP=$!
# (the game's log restarts with the new run: wait for its bigslide 180 line, then 40 s more, then stop the game by PID)
sleep 60
until grep -q 'bigslide 180' $LOG 2>/dev/null || ! kill -0 $RP 2>/dev/null; do sleep 15; done
sleep 40
GP=$(ps -eo pid,args | awk '$2 ~ /loco_st7b.exe/ {print $1}' | head -1)
[ -n "$GP" ] && kill $GP && echo "stopped game $GP $(date +%T)"
wait $RP
cp $LOG $T/log_st7b.txt
grep 'bigslide' $T/log_st7b.txt
rm -f $T/st7b/bin/loco_st7b.exe
echo "st7b done $(date +%T)"
cd $T/st7 || exit 1
mkdir -p $T/shots_st7t
export WINEPREFIX=/tmp/wine_anim
D="Z:$(echo $T/shots_st7t/ | sed 's|/|\\|g')"
EXE=bin/loco_st7t.exe TIMEOUT=4800 nice -n 6 sh tools/run.sh --width 960 --height 540 --play --autoplay locotour --renderevery 6 --quality 1 --shotdir "$D" > $T/run_st7t.txt 2>&1
echo "run st7t exit $?"
cp $LOG $T/log_st7t.txt
grep "locotour" $T/log_st7t.txt | grep -v "locotour shot \|big slide" | tail -30
rm -f $T/st7/bin/loco_st7t.exe $T/shots_st7t/*.bmp
echo "orch22 done $(date +%T)"
