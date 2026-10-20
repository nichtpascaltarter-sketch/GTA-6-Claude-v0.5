#!/bin/bash
# AI agent (set 19): after chain k - the other car of q_ramp_s19b_1's hard collision (car 62, straight through node 5038)
# watched: was it slowed inside the box by the s19 preview of the next junction?
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19l.done
while [ ! -f $T/chain19k.done ]; do sleep 15; done
TS_BIN=$T/traffic_sim19 $R $T/w62_ramp_s19b_1.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 3.35 --seed 1 -v --watch 62 190 199.5
echo done > $T/chain19l.done
