#!/bin/bash
# AI agent (set 19): after chain n - car 110 of p_mid_s19v (v3), stuck three times at 3636 2851 with nothing ahead and the
# throttle on (t=165-183), watched (the same run on the same binary)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19o.done
while [ ! -f $T/chain19n.done ]; do sleep 15; done
TS_BIN=$T/traffic_sim19v $R $T/w110_mid_s19v.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 3.1 -v --watch 110 150 184
echo done > $T/chain19o.done
