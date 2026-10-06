#!/bin/bash
# AI agent: once the nt_ai72 build (PID given) has ended, run the test queue (its first line is nt_ai72's couple run).
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
BP=$1
while [ -d /proc/$BP ]; do sleep 20; done
echo "$(date +%T) build $BP ended: $(tail -1 $AW/build_nt_ai72.out); queue runner $$ starts" >> $AW/run_queue.log
exec $AW/ai_queue.sh >> $AW/run_queue.log 2>&1
