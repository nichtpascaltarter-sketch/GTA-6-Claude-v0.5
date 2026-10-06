#!/bin/bash
# AI agent: a test queue - one game at a time: the first line of aiwork/queue.txt ("EXE MODE:DUR:EVERY[:SHOTS[:WxH]]")
# whose exe is ready ("#" lines are skipped; "STOP" ends it). An exe whose build is still going (aiwork/build_TAG.out
# without its "built" line) is waited for while the lines after it run; one whose build failed is dropped. The queue
# file may be edited while this runs - under its lock, as qedit.py does (flock aiwork/queue.lock): a line is taken off
# only as its run starts. Ends after 15 minutes with nothing to run. The runner's "MODE rc=..." lines go to stdout.
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
MAIN=/home/user/GTA-6-Claude-v0.5
Q=$AW/queue.txt
idle=0
while :; do
  PICK=
  STOP=
  while IFS= read -r LINE; do
    [ -z "$LINE" ] && continue
    case "$LINE" in \#*) continue ;; esac
    [ "$LINE" = "STOP" ] && { STOP=1; break; }
    EXE=${LINE%% *}
    TAG=$(basename "$EXE" .exe)
    B=$AW/build_$TAG.out
    if [ -f "$B" ] && ! grep -q "built $EXE" "$B"; then
      if grep -qE "MINE|foreign break" "$B"; then
        echo "$(date +%T) $TAG did not build: dropping \"$LINE\""
        flock $AW/queue.lock python3 $AW/qedit.py drop "$LINE"
      fi
      continue   # (still building)
    fi
    [ -f "$MAIN/$EXE" ] || continue
    PICK=$LINE
    break
  done < <(flock $AW/queue.lock cat $Q 2>/dev/null)
  [ -n "$STOP" ] && break
  if [ -z "$PICK" ]; then
    idle=$((idle + 1))
    [ $idle -gt 30 ] && break
    sleep 30
    continue
  fi
  idle=0
  flock $AW/queue.lock python3 $AW/qedit.py drop "$PICK"
  $AW/ai_runtests2.sh "${PICK%% *}" "${PICK#* }"
done
echo "$(date +%T) queue done"
