#!/bin/sh
# Usage nearly out (user, 10-20 ~17:50): let snapshot 63's smoke finish and push itself, give the agents time to
# write their resume notes, then save everything to claude/neon-tide-save.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
n=0; until grep -qE "COMMITTED|FAILED|HOLD" $SP/snap63_run.log || [ $n -ge 240 ]; do sleep 15; n=$((n+1)); done
sleep 120
bash $SP/save_all.sh > $SP/lead/save_all.log 2>&1
echo "PARK SAVE DONE $(date -u +%H:%M)" >> $SP/lead/save_all.log
