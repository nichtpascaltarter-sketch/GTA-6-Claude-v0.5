#!/bin/bash
# AI agent (set 19): base harness from HEAD + the s19 overlay build, then before/after runs (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19e.done
bash $AW/ts_build.sh all HEAD > $AW/ts_build_all.out 2>&1
grep -q built $AW/ts_build_all.out || { echo "base build failed" > $T/chain19e.done; exit 1; }
bash $AW/ts_build19.sh > $AW/ts_build19.out 2>&1
grep -q built $AW/ts_build19.out || { echo "s19 build failed" > $T/chain19e.done; exit 1; }
for v in base s19; do
  B=$T/traffic_sim; [ $v = s19 ] && B=$T/traffic_sim19
  TS_BIN=$B $R $T/r_ramp_$v.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 -v
  TS_BIN=$B $R $T/r_mid_$v.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 -v
done
TS_BIN=$T/traffic_sim19 $R $T/r_dt1_s19.txt --sim 2700 760 450 --cars 140 --peds 120 --minutes 8 -v
TS_BIN=$T/traffic_sim19 $R $T/r_luna_s19.txt --sim 2250 100 450 --cars 140 --peds 120 --minutes 6 -v
TS_BIN=$T/traffic_sim19 $R $T/r_hwy_s19.txt --sim 2342 1143 600 --cars 160 --peds 40 --minutes 6 -v
TS_BIN=$T/traffic_sim19 $R $T/r_beach_s19.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 -v
TS_BIN=$T/traffic_sim $R $T/walkcheck_all.txt --walkcheck all
echo done > $T/chain19e.done
