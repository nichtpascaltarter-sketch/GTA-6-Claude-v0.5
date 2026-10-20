#!/bin/bash
# AI agent (set 19): after ts_chain19.sh, the map-wide walk-link check (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
rm -f $SP/ts/chain19b.done
while [ ! -f $SP/ts/chain19.done ]; do sleep 15; done
grep -q built $AW/ts_build_ai.out && $AW/ts_run.sh $SP/ts/walkcheck.txt --walkcheck
echo done > $SP/ts/chain19b.done
