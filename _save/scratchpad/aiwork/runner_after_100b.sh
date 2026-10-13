#!/bin/bash
# AI agent: once nt_ai100b is built, make sure the queue runner is there for its runs (copreturn, taxi, crashscene) -
# nothing else is queued (wind-down: set 17 stays parked)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
until grep -qE "built bin/nt_ai100b|MINE|foreign" $AW/build_nt_ai100b.out 2>/dev/null; do sleep 20; done
R=$(cat $AW/queue_runner.pid)
if ! kill -0 $R 2>/dev/null; then
  P=$($AW/ai_detach.sh /dev/null bash -c "exec $AW/ai_queue.sh >> $AW/run_queue.log 2>&1")
  echo $P > $AW/queue_runner.pid
  echo "$(date +%T) restarted the queue runner: $P"
fi
echo "$(date +%T) done"
