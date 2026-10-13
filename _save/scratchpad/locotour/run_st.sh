#!/bin/sh
# usage: run_st.sh <tree>   - one locostairs run (a SkyLine station's street stair), the log to log_<t>.txt
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
t=$1
# (one game at a time: after the pass-fix reruns)
until [ -f $T/pass_reruns.done ]; do sleep 30; done
cd $T/$t || exit 1
mkdir -p $T/shots_$t
export WINEPREFIX=/tmp/wine_anim
LOG=/tmp/wine_anim/drive_c/users/root/AppData/Local/NeonTide/log.txt
D="Z:$(echo $T/shots_$t/ | sed 's|/|\\|g')"
EXE=bin/loco_$t.exe TIMEOUT=4800 nice -n 6 sh tools/run.sh --width 960 --height 540 --play --autoplay locostairs --renderevery 6 --quality 1 --shotdir "$D" > $T/run_$t.txt 2>&1
echo "run $t exit $?"
cp $LOG $T/log_$t.txt
grep "locostairs" $T/log_$t.txt | tail -12
