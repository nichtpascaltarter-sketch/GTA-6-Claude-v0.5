#!/bin/sh
# detached: st7b = st7 + walker 180 traced (test-only); build now, then after orch18: locostairs on st7b (log_st7b.txt;
# deterministic, so the 30 cm quick-step slide at ~489 s shows frame by frame), then the locotour on st7's copied exe
# (log_st7t.txt). One game at a time; exes and BMPs deleted after.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
echo "start $(date +%T)"
[ -f $T/st7b/bin/loco_st7b.exe ] || sh $T/build.sh st7b
[ -f $T/st7b/bin/loco_st7b.exe ] || { echo "no exe for st7b"; exit 1; }
echo "built st7b $(date +%T)"
until grep -q 'orch18 done' $T/orch18.log 2>/dev/null; do sleep 30; done
for i in 1 2; do
  sh $T/run_st.sh st7b
  grep -q "locostairs trips\|no stair found" $T/log_st7b.txt && break
done
grep "locostairs" $T/log_st7b.txt | grep -v "trace\|locostairs t \|deep" | tail -8
rm -f $T/st7b/bin/loco_st7b.exe
echo "st7b done $(date +%T)"
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
echo "orch20 done $(date +%T)"
