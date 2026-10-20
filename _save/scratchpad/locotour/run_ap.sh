#!/bin/sh
# usage: run_ap.sh <tree> <autoplay> <tag> [extra args]  - one --autoplay run of a locotour tree's exe, its log to log_<tag>.txt
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
t=$1; ap=$2; tag=$3; shift 3
cd $T/$t || exit 1
mkdir -p $T/shots_$tag
export WINEPREFIX=/tmp/wine_anim
LOG=/tmp/wine_anim/drive_c/users/root/AppData/Local/NeonTide/log.txt
D="Z:$(echo $T/shots_$tag/ | sed 's|/|\\|g')"
EXE=bin/loco_$t.exe TIMEOUT=2400 nice -n 6 sh tools/run.sh --width 960 --height 540 --play --autoplay $ap --renderevery 6 --quality 1 --shotdir "$D" "$@" > $T/run_$tag.txt 2>&1
echo "run $tag exit $?"
cp $LOG $T/log_$tag.txt
rm -f $T/shots_$tag/*.bmp
