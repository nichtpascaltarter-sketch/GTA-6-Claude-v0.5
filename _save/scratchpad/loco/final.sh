#!/bin/sh
# final metrics: locometer on base (HEAD animator) and work (= the real tree's anim files), then anim_test on the real tree
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
R=/home/user/GTA-6-Claude-v0.5
cp $R/src/anim/animator.cpp $L/work/src/anim/animator.cpp
cp $R/src/anim/character.h $L/work/src/anim/character.h
for t in base work; do
  for i in 1 2 3 4 5 6 7 8; do sh $L/mk.sh $L/$t $L/lm_$t && break; done
done
$L/lm_base > $L/base_final.txt 2>&1
$L/lm_work > $L/work_final.txt 2>&1
for i in 1 2 3 4 5 6 7 8; do
  sh $L/../car/gate.sh sh -c "cd $R && g++ -O2 -std=c++17 -I src tests/anim/anim_test.cpp -o $L/at_final.new 2>&1 | grep -E 'error|warning' | head -20" && mv $L/at_final.new $L/at_final && break
done
$L/at_final > $L/at_final.log 2>&1
echo "anim_test rc $?"
tail -1 $L/at_final.log
echo done
