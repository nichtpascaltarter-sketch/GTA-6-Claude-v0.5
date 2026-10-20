#!/bin/bash
# AI agent (set 19): one harness job at a time - wait for the AI-part rebuild (ts_build.sh ai, already behind its gate),
# then the map-wide overlap list and the verbose Midtown run, each behind the run gate (ts_run.sh)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
rm -f $SP/ts/chain19.done
while ! grep -qE "built|FAILED" $AW/ts_build_ai.out 2>/dev/null; do sleep 15; done
if grep -q built $AW/ts_build_ai.out; then
  $AW/ts_run.sh $SP/ts/overlaps.txt --overlaps
  $AW/ts_run.sh $SP/ts/sim_mid_v.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 -v --plot $SP/ts/mid.ppm
fi
echo done > $SP/ts/chain19.done
