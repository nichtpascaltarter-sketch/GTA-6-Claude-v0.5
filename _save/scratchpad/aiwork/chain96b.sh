#!/bin/bash
# AI agent: after the nt_ai99b build, rebuild nt_ai96 (c1af87a + taxi ef10180 + calmActor ff4a3f8 + test app: the pair for
# snapshot 47) for its knock run; once that run has started, queue nt_ai99b's taxi + crashscene, then the held set 17 runs.
# Restarts the queue runner if it has gone idle-out meanwhile.
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
B99=$(cat $AW/build99b.pid)
while kill -0 $B99 2>/dev/null; do sleep 15; done
bash $AW/ai_buildgraft.sh c1af87a $AW/graft96.txt bin/nt_ai96.exe > $AW/build_nt_ai96.out 2>&1
echo "$(date +%T) nt_ai96 build done: $(tail -1 $AW/build_nt_ai96.out)"
R=$(cat $AW/queue_runner.pid)
if ! kill -0 $R 2>/dev/null; then
  P=$($AW/ai_detach.sh /dev/null bash -c "exec $AW/ai_queue.sh >> $AW/run_queue.log 2>&1")
  echo $P > $AW/queue_runner.pid
  echo "$(date +%T) restarted the queue runner: $P"
fi
until grep -q "nt_ai96.exe knock" <(ps -eo args) || ! grep -q "nt_ai96.exe knock" $AW/queue.txt; do sleep 10; done
sleep 5
while IFS= read -r l; do [ -n "$l" ] && flock $AW/queue.lock python3 $AW/qedit.py add "$l"; done < $AW/queue_held99b.txt
while IFS= read -r l; do [ -n "$l" ] && flock $AW/queue.lock python3 $AW/qedit.py add "$l"; done < $AW/queue_held97.txt
echo "$(date +%T) queued nt_ai99b and the set 17 runs"
