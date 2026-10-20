#!/bin/bash
# AI agent (set 19): after ts_chain19b.sh, the soak's stuck spots (the ramp at 2465 3880, the lane behind the double-parked
# truck at 2471 3752): lane probes and a map, then a verbose sim of the area (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
rm -f $SP/ts/chain19c.done
while [ ! -f $SP/ts/chain19b.done ]; do sleep 15; done
$AW/ts_run.sh $SP/ts/soakprobe.txt --nearlane 2465 3880 0 1 --nearlane 2471 3752 0 1 --lanes 2468 3820 160 $SP/ts/soak_area.ppm --lane 19795 0 400
$AW/ts_run.sh $SP/ts/sim_soak_v.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 -v --plot $SP/ts/soak.ppm
echo done > $SP/ts/chain19c.done
