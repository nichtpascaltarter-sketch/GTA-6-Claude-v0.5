#!/bin/sh
# usage: run_st2.sh <tree> <tag> [stops]   - one locostairs run (LOCO_STOPS=1 with "stops"), the log to log_<tag>.txt
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
t=$1; tag=$2
cd $T/$t || exit 1
mkdir -p $T/shots_$tag
export WINEPREFIX=/tmp/wine_anim
[ "$3" = stops ] && export LOCO_STOPS=1
LOG=/tmp/wine_anim/drive_c/users/root/AppData/Local/NeonTide/log.txt
rm -f $LOG
D="Z:$(echo $T/shots_$tag/ | sed 's|/|\\|g')"
EXE=bin/loco_$t.exe TIMEOUT=4800 nice -n 6 sh tools/run.sh --width 960 --height 540 --play --autoplay locostairs --renderevery 6 --quality 1 --shotdir "$D" > $T/run_$tag.txt 2>&1
echo "run $tag exit $?"
[ -f $LOG ] && cp $LOG $T/log_$tag.txt
grep "locostairs" $T/log_$tag.txt 2>/dev/null | grep -v "trace\|locostairs t \|deep" | tail -8
