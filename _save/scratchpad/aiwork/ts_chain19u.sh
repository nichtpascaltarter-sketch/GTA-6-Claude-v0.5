#!/bin/bash
# AI agent (set 19): the whole comparison for v7 (traffic_sim19y, after ts_chain19t.sh built it): the same runs as v6's.
# Restart-safe: runs whose output ends in rc=0 are skipped - after a restart relaunch it:
#   AW/ai_detach.sh AW/ts_chain19u.out bash AW/ts_chain19u.sh   (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19u.done
once() { local out=$1; shift; grep -q " rc=0" "$out" 2>/dev/null && return 0; "$@"; }
B=$T/traffic_sim19y
[ -x $B ] || { echo "no traffic_sim19y" > $T/chain19u.done; exit 1; }
# (first: the fire engine's head-on with a truck at the end of v7's Sol Beach seed 1)
once $T/w17_beach_s19y_1.txt env TS_BIN=$B $R $T/w17_beach_s19y_1.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 --seed 1 -v --watch 17 335 360
for s in 1 2 3; do
  once $T/q_mid_s19y_$s.txt env TS_BIN=$B $R $T/q_mid_s19y_$s.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
  once $T/q_ramp_s19y_$s.txt env TS_BIN=$B $R $T/q_ramp_s19y_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
done
for d in "dt1 2700 760 450 140 120 8" "luna 2250 100 450 140 120 6" "hwy 2342 1143 600 160 40 6"; do
  set -- $d
  once $T/r_$1_s19y.txt env TS_BIN=$B $R $T/r_$1_s19y.txt --sim $2 $3 $4 --cars $5 --peds $6 --minutes $7 -v
done
once $T/p_mid_s19y.txt env TS_BIN=$B $R $T/p_mid_s19y.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
once $T/p_mid_s19y_2.txt env TS_BIN=$B $R $T/p_mid_s19y_2.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 --seed 2 -v
once $T/p_ramp_s19y.txt env TS_BIN=$B $R $T/p_ramp_s19y.txt --sim 2468 3820 320 --cars 120 --peds 60 --parked 90 --minutes 5 -v
once $T/p_dt1_s19y.txt env TS_BIN=$B $R $T/p_dt1_s19y.txt --sim 2700 760 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
echo done > $T/chain19u.done
