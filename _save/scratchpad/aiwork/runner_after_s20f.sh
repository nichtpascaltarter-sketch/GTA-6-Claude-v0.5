#!/bin/bash
# AI agent (set 20): start the test queue once bin/nt_s20f.exe is built (the queue gives up after 15 idle minutes)
A=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
while [ ! -f /home/user/GTA-6-Claude-v0.5/bin/nt_s20f.exe ]; do
  grep -q "nt_s20f rc=[1-9]" $A/bld_s20f.out 2>/dev/null && { echo "nt_s20f build failed"; exit 1; }
  sleep 30
done
echo $$ > $A/queue_runner.pid
exec bash $A/ai_queue.sh
