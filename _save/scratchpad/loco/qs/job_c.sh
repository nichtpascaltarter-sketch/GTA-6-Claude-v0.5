#!/bin/sh
# job_c.sh: after job_b, c14b's anim_test (attree15 with clean14b) -> sl/at_qs14b.log
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
while ! grep -q "job_b done" $L/qs/job_b.log 2>/dev/null; do sleep 30; done
cp $L/qs/clean14b/animator_qs14b.cpp $L/attree15/src/anim/animator.cpp
sh $L/qs/at15.sh
mv $L/sl/at_qs14.log $L/sl/at_qs14b.log
echo "job_c done"
