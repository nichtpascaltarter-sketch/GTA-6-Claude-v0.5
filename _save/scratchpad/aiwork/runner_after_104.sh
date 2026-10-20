#!/bin/bash
# AI agent: once nt_ai104's build has ended (built / failed), make sure the queue runner is going (it gives up after 15
# idle minutes, and the build may wait longer than that for memory)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
until grep -qE "built bin/nt_ai104.exe|MINE|foreign break" $AW/build_nt_ai104.out 2>/dev/null || ! kill -0 $(cat $AW/build104.pid) 2>/dev/null; do sleep 30; done
if [ -z "$(ps -eo args | awk '$2 ~ /ai_queue\.sh$/ {print}')" ]; then
  $AW/ai_detach.sh /dev/null bash -c "exec $AW/ai_queue.sh >> $AW/run_queue.log 2>&1" > $AW/queue_runner.pid
fi
