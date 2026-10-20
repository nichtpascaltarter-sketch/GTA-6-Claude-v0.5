#!/bin/bash
# Stop the AI agent's own build loop (ai_buildloop.sh, PID in ai_buildloop.pid) and every process descended from it -
# nothing else: other agents' compilers look the same in ps.
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
ROOT=$(cat $AW/ai_buildloop.pid 2>/dev/null)
[ -n "$ROOT" ] && [ -d /proc/$ROOT ] || { echo "no build loop running"; exit 0; }
grep -q "ai_buildloop.sh" /proc/$ROOT/cmdline 2>/dev/null || { echo "PID $ROOT is not the build loop"; exit 1; }
ALL="$ROOT"; FRONT="$ROOT"
while [ -n "$FRONT" ]; do
  NEXT=""
  for p in $FRONT; do NEXT="$NEXT $(ps -o pid= --ppid $p)"; done
  FRONT=$(echo $NEXT); ALL="$ALL $FRONT"
done
echo "stopping: $(for p in $ALL; do ps -o pid=,cmd= -p $p | cut -c1-60; done)"
kill $ALL 2>/dev/null
