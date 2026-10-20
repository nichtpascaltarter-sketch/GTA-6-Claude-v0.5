#!/bin/sh
# showcase set 4 waits for snapshot 56 to end, then for the autoupdater's starved compile gate (PID 14554) to pass,
# at most 40 min past the snapshot, so its build gets the next slot before the 4 GB showcase takes one
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
until grep -qE "COMMITTED|FAILED|HOLD" $SP/snap56_run.log; do sleep 20; done
n=0; while kill -0 14554 2>/dev/null && [ $n -lt 120 ]; do sleep 20; n=$((n+1)); done
echo "snapshot 56 ended, autoupdater gate passed or timed out: set 4 at $(date -u +%H:%M)"
exec sh $SP/showcase.sh 4
