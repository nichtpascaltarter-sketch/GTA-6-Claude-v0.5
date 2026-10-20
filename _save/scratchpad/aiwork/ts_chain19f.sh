#!/bin/bash
# AI agent (set 19): after chain e - rebuild both harness binaries with --seed, then base vs s19 over three seeds in the
# ramp area and Midtown, and base runs of the other districts (chain e ran them on s19) - one harness job at a time
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19f.done
while [ ! -f $T/chain19e.done ]; do sleep 15; done
cp /home/user/GTA-6-Claude-v0.5/tools/traffic_sim.cpp $T/tree/tools/traffic_sim.cpp
bash $AW/ts_build.sh ai > $AW/ts_build_ai4.out 2>&1
grep -q built $AW/ts_build_ai4.out || { echo "base ai build failed" > $T/chain19f.done; exit 1; }
bash $AW/ts_build19.sh > $AW/ts_build19b.out 2>&1
grep -q built $AW/ts_build19b.out || { echo "s19 build failed" > $T/chain19f.done; exit 1; }
for s in 1 2 3; do
  for v in base s19; do
    B=$T/traffic_sim; [ $v = s19 ] && B=$T/traffic_sim19
    TS_BIN=$B $R $T/q_ramp_${v}_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
    TS_BIN=$B $R $T/q_mid_${v}_$s.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
  done
done
TS_BIN=$T/traffic_sim $R $T/r_dt1_base.txt --sim 2700 760 450 --cars 140 --peds 120 --minutes 8 -v
TS_BIN=$T/traffic_sim $R $T/r_luna_base.txt --sim 2250 100 450 --cars 140 --peds 120 --minutes 6 -v
TS_BIN=$T/traffic_sim $R $T/r_hwy_base.txt --sim 2342 1143 600 --cars 160 --peds 40 --minutes 6 -v
TS_BIN=$T/traffic_sim $R $T/r_beach_base.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 -v
echo done > $T/chain19f.done
