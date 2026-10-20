#!/bin/bash
# AI agent (set 19, after the 02:19 container restart - replaces chains f (its unfinished tail), g and h): rebuild the base
# harness (wider --overlaps) and s19 v2 (+ box mutual blocks), then the overlap list again, the v2 runs on the ramp and
# Midtown seeds, base and v2 runs of the other districts, and the walk maps (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19i.done
cp /home/user/GTA-6-Claude-v0.5/tools/traffic_sim.cpp $T/tree/tools/traffic_sim.cpp
bash $AW/ts_build.sh ai > $AW/ts_build_ai5.out 2>&1
grep -q built $AW/ts_build_ai5.out || { echo "base ai build failed" > $T/chain19i.done; exit 1; }
bash $AW/ts_build19.sh > $AW/ts_build19c.out 2>&1
grep -q built $AW/ts_build19c.out || { echo "s19 build failed" > $T/chain19i.done; exit 1; }
TS_BIN=$T/traffic_sim $R $T/overlaps2.txt --overlaps
for s in 1 2 3; do
  TS_BIN=$T/traffic_sim19 $R $T/q_ramp_s19b_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
  TS_BIN=$T/traffic_sim19 $R $T/q_mid_s19b_$s.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
done
for d in "dt1 2700 760 450 140 120 8" "luna 2250 100 450 140 120 6" "hwy 2342 1143 600 160 40 6" "beach 5250 600 450 140 120 6"; do
  set -- $d
  TS_BIN=$T/traffic_sim $R $T/r_$1_base.txt --sim $2 $3 $4 --cars $5 --peds $6 --minutes $7 -v
  TS_BIN=$T/traffic_sim19 $R $T/r_$1_s19b.txt --sim $2 $3 $4 --cars $5 --peds $6 --minutes $7 -v
done
$R $T/walkmaps.txt --lanes 4900 8550 150 $T/wm_ne1.ppm --lanes 4600 7800 150 $T/wm_ne2.ppm --lanes 2984 -2049 70 $T/wm_keylime.ppm --lanes 3584 -1849 70 $T/wm_18th.ppm --lanes 2400 -4400 150 $T/wm_s1.ppm --lanes 3700 -330 90 $T/wm_bay11.ppm
echo done > $T/chain19i.done
