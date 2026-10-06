#!/bin/bash
# the sign-off set (nt_ai52's AI files + the held-up bail-out) built in its own tree: bin/nt_ai52f.exe
SF=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/snapfix
/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/memgate.sh 3200
cd $SF && QUICK=1 OUT=bin/nt_ai52f.exe nice -n 5 ./build.sh > build.log 2>&1 && echo "built nt_ai52f $(date +%T)" || { echo "FAILED"; grep -E "error|Killed" build.log | head -5; }
