#!/bin/sh
# transit agent: build (memory-gated; retried while another agent's file is mid-edit) then run the transit tests one at a time
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
ok=0
for attempt in 1 2 3 4 5 6 7 8; do
  $S/memgate.sh 2600
  $S/tr_build.sh > /dev/null
  if grep -q "^exit 0" $S/tr_build.txt; then ok=1; break; fi
  if grep -E "error" $S/tr_build.txt | grep -q "transit"; then break; fi   # my own error: stop and report
  echo "build attempt $attempt failed in another agent's file, retrying in 3 min:"; grep -E "error" $S/tr_build.txt | head -3
  sleep 180
done
if [ $ok = 0 ]; then echo "BUILD FAILED"; grep -E "error" $S/tr_build.txt | head -30; exit 1; fi
for m in ${MODES:-metro bus ferry}; do
  $S/memgate.sh 2600
  MODES=$m $S/tr_tests.sh
  echo "== $m"; cat /tmp/transit/summary_$m.txt | cut -c1-200 | grep -E "Transit test|error|Error" | head -40
done
echo "cycle done"
