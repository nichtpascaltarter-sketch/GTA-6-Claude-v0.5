#!/bin/sh
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /tmp/leadtree3 || exit 1
sh $SP/memgate.sh 3000 >/dev/null 2>&1
QUICK=1 OUT=bin/lead_q.exe ./build.sh > $SP/lead_q_build.log 2>&1
echo "rc=$?" >> $SP/lead_q_build.log
