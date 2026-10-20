#!/bin/bash
# AI agent (set 19): after chains f and g - rebuild base (harness: wider --overlaps) and s19 (v2: + box mutual blocks),
# then the overlap list again and the v2 runs on the same seeds (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19h.done
while [ ! -f $T/chain19f.done ] || [ ! -f $T/chain19g.done ]; do sleep 15; done
cp /home/user/GTA-6-Claude-v0.5/tools/traffic_sim.cpp $T/tree/tools/traffic_sim.cpp
bash $AW/ts_build.sh ai > $AW/ts_build_ai5.out 2>&1
grep -q built $AW/ts_build_ai5.out || { echo "base ai build failed" > $T/chain19h.done; exit 1; }
bash $AW/ts_build19.sh > $AW/ts_build19c.out 2>&1
grep -q built $AW/ts_build19c.out || { echo "s19 build failed" > $T/chain19h.done; exit 1; }
TS_BIN=$T/traffic_sim $R $T/overlaps2.txt --overlaps
for s in 1 2 3; do
  TS_BIN=$T/traffic_sim19 $R $T/q_ramp_s19b_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
  TS_BIN=$T/traffic_sim19 $R $T/q_mid_s19b_$s.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
done
TS_BIN=$T/traffic_sim19 $R $T/r_beach_s19b.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 -v
TS_BIN=$T/traffic_sim19 $R $T/r_dt1_s19b.txt --sim 2700 760 450 --cars 140 --peds 120 --minutes 8 -v
echo done > $T/chain19h.done
