#!/bin/bash
# AI agent (set 19, after the 03:37 container restart - replaces chains k and l, killed): base harness rebuilt from HEAD
# 837b39c (world + AI, shared tree's traffic_sim.cpp: strict --overlaps, --parked), --overlaps check (31 pairs), s19 v3
# build (traffic_sim19v), the car-62 watch on the v2 binary (traffic_sim19, kept from 02:55), then the v3 runs: parked
# areas, the regression seeds, a second parked seed for base and v3 (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19m.done
bash $AW/ts_build.sh all HEAD > $AW/ts_build_all2.out 2>&1
grep -q built $AW/ts_build_all2.out || { echo "base build failed" > $T/chain19m.done; exit 1; }
TS_BIN=$T/traffic_sim $R $T/overlaps4.txt --overlaps
V=v bash $AW/ts_build19v.sh > $AW/ts_build19v.out 2>&1
grep -q built $AW/ts_build19v.out || { echo "s19v build failed" > $T/chain19m.done; exit 1; }
TS_BIN=$T/traffic_sim19 $R $T/w62_ramp_s19b_1.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 3.35 --seed 1 -v --watch 62 190 199.5
B=$T/traffic_sim19v
TS_BIN=$B $R $T/p_ramp_s19v.txt --sim 2468 3820 320 --cars 120 --peds 60 --parked 90 --minutes 5 -v
TS_BIN=$B $R $T/p_mid_s19v.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
TS_BIN=$B $R $T/p_dt1_s19v.txt --sim 2700 760 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
for s in 1 2 3; do
  TS_BIN=$B $R $T/q_ramp_s19v_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
  TS_BIN=$B $R $T/q_mid_s19v_$s.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
done
for v in base s19v; do
  BB=$T/traffic_sim; [ $v = s19v ] && BB=$B
  TS_BIN=$BB $R $T/p_mid_${v}_2.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 --seed 2 -v
  TS_BIN=$BB $R $T/p_ramp_${v}_2.txt --sim 2468 3820 320 --cars 120 --peds 60 --parked 90 --minutes 5 --seed 2 -v
done
echo done > $T/chain19m.done
