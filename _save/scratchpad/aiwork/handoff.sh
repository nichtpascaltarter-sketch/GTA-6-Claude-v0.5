#!/bin/bash
# AI agent (one-off): once the orphaned runner 1710 has finished its life run, stop it before its next game and hand
# the rest over to the queue (ai_queue.sh)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
R=1710
ok() { kill -0 $R 2>/dev/null && ps -o args= -p $R | grep -q "ai_runtests.sh bin/nt_ai62.exe life"; }
until grep -q "^life rc=" $AW/run_chain_resume.log 2>/dev/null || ! ok; do sleep 1; done
if ok; then
  KIDS=$(ps -o pid= --ppid $R)
  kill $R
  for k in $KIDS; do
    for g in $(ps -o pid= --ppid $k); do kill $g 2>/dev/null; done
    kill $k 2>/dev/null
  done
  WINEPREFIX=/tmp/wine_ai /usr/lib/wine/wineserver64 -k 2>/dev/null
  echo "$(date +%T) stopped runner $R after its life run (children: $KIDS)"
fi
exec $AW/ai_queue.sh
