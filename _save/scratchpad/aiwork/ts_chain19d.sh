#!/bin/bash
# AI agent (set 19): after ts_chain19c.sh and the probe rebuild (ts_build_ai2.out), the Midtown red runs at nodes 272 / 274
# (cars aiming to stop ~4.7 m past the line the counter uses): node and lane probes in one run
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
rm -f $SP/ts/chain19d.done
while [ ! -f $SP/ts/chain19c.done ] || ! grep -qE "built|FAILED" $AW/ts_build_ai2.out 2>/dev/null; do sleep 15; done
grep -q built $AW/ts_build_ai2.out && $AW/ts_run.sh $SP/ts/redprobe.txt --node 272 --node 274 --lane 1010 0 200 --lane 1013 0 200 --lane 1014 0 200
echo done > $SP/ts/chain19d.done
