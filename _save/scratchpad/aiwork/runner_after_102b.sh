#!/bin/bash
# AI agent: once nt_ai102b is built, make sure the queue runner is going (it ends after 15 idle minutes)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
until grep -qE "built|MINE|foreign" $AW/build_nt_ai102b.out 2>/dev/null; do sleep 30; done
R=$(cat $AW/queue_runner.pid 2>/dev/null)
if [ -n "$R" ] && [ -d /proc/$R ] && grep -q "ai_queue.sh" /proc/$R/cmdline 2>/dev/null; then echo "$(date +%T) runner $R still going"; exit 0; fi
N=$($AW/ai_detach.sh /dev/null bash -c "exec $AW/ai_queue.sh >> $AW/run_queue.log 2>&1")
echo $N > $AW/queue_runner.pid
echo "$(date +%T) runner restarted: $N"
