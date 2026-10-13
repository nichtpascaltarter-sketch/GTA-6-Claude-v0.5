#!/bin/sh
# anim_test on attree4 (the candidate), gated host compile, after the candidate's harness build
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
O=/dev/null

for i in 1 2 3 4 5 6; do
  sh $L/../car/gate.sh sh -c "cd $L/attree4 && g++ -O2 -std=c++17 -I . -I src tests/anim/anim_test.cpp -o $L/sl/at_qs.new 2>&1 | grep -E 'error|warning' | head -20" && [ -f $L/sl/at_qs.new ] && mv $L/sl/at_qs.new $L/sl/at_qs && break
done
cd $L/attree4 && $L/sl/at_qs > $L/sl/at_qs.log 2>&1
echo "anim_test rc $?"
tail -1 $L/sl/at_qs.log
rm -f $L/sl/at_qs
