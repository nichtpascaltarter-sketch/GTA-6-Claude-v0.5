#!/bin/sh
# usage: run_ap2.sh <tree> <exe name> <autoplay> <tag>  - one --autoplay run of that exe, its log to log_<tag>.txt
# (10-20 07:5x: --renderevery 30 for the new runs - the sim steps 1/30 s fixed and animates every step whatever is drawn,
# rendering only costs time; the c12 sign-off pairs st16* / st17* keep their 6. TIMEOUT 3 h: a locoplayer run is long)
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
t=$1; exe=$2; ap=$3; tag=$4
cd $T/$t || exit 1
mkdir -p $T/shots_$tag
export WINEPREFIX=/tmp/wine_anim
LOG=/tmp/wine_anim/drive_c/users/root/AppData/Local/NeonTide/log.txt
rm -f $LOG
D="Z:$(echo $T/shots_$tag/ | sed 's|/|\\|g')"
RE=30; case $tag in st16*|st17*) RE=6;; esac
EXE=bin/$exe TIMEOUT=10800 nice -n 6 sh tools/run.sh --width 960 --height 540 --play --autoplay $ap --renderevery $RE --quality 1 --shotdir "$D" > $T/run_$tag.txt 2>&1
echo "run $tag exit $? (renderevery $RE)"
[ -f $LOG ] && cp $LOG $T/log_$tag.txt
rm -f $T/shots_$tag/*.bmp
grep "locotour" $T/log_$tag.txt 2>/dev/null | grep "steps \|feet:\|slides over" | tail -8
