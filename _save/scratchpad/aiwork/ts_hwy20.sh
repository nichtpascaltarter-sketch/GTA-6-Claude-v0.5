#!/bin/bash
# AI agent (set 20, item 4): highway lane keeping for one harness build: the I-38 ramps (3 seeds) and the highway run on
# HEAD's world (tree61). Usage: ts_hwy20.sh V   (binary SP/ts/traffic_sim20$V -> SP/ts/h_{ramp_V_s,hwy_V}.txt)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
V=$1
B=$T/traffic_sim20$V
once() { local out=$1; shift; grep -q " rc=0" "$out" 2>/dev/null && return 0; "$@"; }
for s in 1 2 3; do
  once $T/h_ramp_${V}_$s.txt env TS_TREE=tree61 TS_BIN=$B $R $T/h_ramp_${V}_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
done
once $T/h_hwy_${V}.txt env TS_TREE=tree61 TS_BIN=$B $R $T/h_hwy_${V}.txt --sim 2342 1143 600 --cars 160 --peds 40 --minutes 6 -v
echo done > $T/hwy20_$V.done
