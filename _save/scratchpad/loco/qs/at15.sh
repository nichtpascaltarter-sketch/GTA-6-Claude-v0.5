#!/bin/sh
# anim_test on attree15 (HEAD 837b39c + clean14: c13 + late re-scan keeps its tread + stairs seen by distance)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
for i in 1 2 3 4 5 6; do
  sh $L/cgate.sh sh -c "cd $L/attree15 && g++ -O2 -std=c++17 -I . -I src tests/anim/anim_test.cpp -o $L/sl/at_qs14.new 2>&1 | grep -E 'error|warning' | head -20" && [ -f $L/sl/at_qs14.new ] && mv $L/sl/at_qs14.new $L/sl/at_qs14 && break
done
cd $L/attree15 && $L/sl/at_qs14 > $L/sl/at_qs14.log 2>&1
echo "anim_test rc $?"
tail -1 $L/sl/at_qs14.log
rm -f $L/sl/at_qs14
