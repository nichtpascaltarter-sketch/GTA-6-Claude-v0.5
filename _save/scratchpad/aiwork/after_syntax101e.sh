#!/bin/bash
# AI agent: once the nt_ai101e syntax check (set 17's wheel fix) is over, put the held set 18 runs back (gotofollow first) and
# make sure the queue runner is going
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
until grep -q "syntax check done\|no room" $AW/syntax101e.out 2>/dev/null; do sleep 15; done
cd $AW
flock queue.lock sh -c "{ echo 'bin/nt_ai102b.exe gotofollow:150:12:60:960x540'; cat queue_held102b.txt; cat queue.txt; } > queue.new && mv queue.new queue.txt"
R=$(cat $AW/queue_runner.pid 2>/dev/null)
if [ -n "$R" ] && [ -d /proc/$R ] && grep -q "ai_queue.sh" /proc/$R/cmdline 2>/dev/null; then echo "$(date +%T) queue restored, runner $R still going"; exit 0; fi
N=$($AW/ai_detach.sh /dev/null bash -c "exec $AW/ai_queue.sh >> $AW/run_queue.log 2>&1")
echo $N > $AW/queue_runner.pid
echo "$(date +%T) queue restored, runner restarted: $N"
