#!/bin/bash
# AI agent (set 19): after chain i - rebuild base + s19 from the shared tree's harness (strict --overlaps restored, --parked),
# check --overlaps reproduces the 31-pair list, then parked-car runs (base) in the soak's ramp area and Midtown, and the
# hard collision of q_ramp_s19b_1 (t=198.9, cars 62 / 118 at node 5038) watched (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19j.done
while [ ! -f $T/chain19i.done ]; do sleep 15; done
cp /home/user/GTA-6-Claude-v0.5/tools/traffic_sim.cpp $T/tree/tools/traffic_sim.cpp
bash $AW/ts_build.sh ai > $AW/ts_build_ai6.out 2>&1
grep -q built $AW/ts_build_ai6.out || { echo "base ai build failed" > $T/chain19j.done; exit 1; }
bash $AW/ts_build19.sh > $AW/ts_build19d.out 2>&1
grep -q built $AW/ts_build19d.out || { echo "s19 build failed" > $T/chain19j.done; exit 1; }
TS_BIN=$T/traffic_sim $R $T/overlaps3.txt --overlaps
TS_BIN=$T/traffic_sim $R $T/p_ramp_base.txt --sim 2468 3820 320 --cars 120 --peds 60 --parked 90 --minutes 5 -v
TS_BIN=$T/traffic_sim $R $T/p_mid_base.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
TS_BIN=$T/traffic_sim $R $T/p_dt1_base.txt --sim 2700 760 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
TS_BIN=$T/traffic_sim19 $R $T/w_ramp_s19b_1.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 3.35 --seed 1 -v --watch 118 186 199.5
echo done > $T/chain19j.done
