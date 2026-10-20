#!/bin/bash
# AI agent (set 19): takes over from chain m once its v3 parked runs are done (chain m stopped there: its v3 seeds are
# superseded) - s19 v5 = v3 + the permissive-left line commit fix + the pass room fix (no shoulder on a divided road's
# median side) as traffic_sim19w; then v5 against base: seeds without parked cars, parked areas, second parked seed,
# Sol Beach seeds, the other districts (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19n.done
until grep -q "rc=" $T/p_dt1_s19v.txt 2>/dev/null; do sleep 15; done
for p in $(ps -eo pid,args | awk '$2 == "bash" && $3 ~ /ts_chain19m\.sh$/ {print $1}'); do kill $p; done
# (the job chain m had started meanwhile finishes on its own: wait for it - one harness job at a time)
while [ -n "$(ps -eo args | awk '$1 ~ /scratchpad\/ts\/traffic_sim/ {print}')" ]; do sleep 10; done
V=w bash $AW/ts_build19v.sh > $AW/ts_build19w.out 2>&1
grep -q built $AW/ts_build19w.out || { echo "s19w build failed" > $T/chain19n.done; exit 1; }
B=$T/traffic_sim19w
for s in 1 2 3; do
  TS_BIN=$B $R $T/q_ramp_s19w_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
  TS_BIN=$B $R $T/q_mid_s19w_$s.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
done
TS_BIN=$B $R $T/p_ramp_s19w.txt --sim 2468 3820 320 --cars 120 --peds 60 --parked 90 --minutes 5 -v
TS_BIN=$B $R $T/p_mid_s19w.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
TS_BIN=$B $R $T/p_dt1_s19w.txt --sim 2700 760 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
for v in base s19w; do
  BB=$T/traffic_sim; [ $v = s19w ] && BB=$B
  TS_BIN=$BB $R $T/p_mid_${v}_2.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 --seed 2 -v
done
for s in 1 2; do
  for v in base s19w; do
    BB=$T/traffic_sim; [ $v = s19w ] && BB=$B
    TS_BIN=$BB $R $T/q_beach_${v}_$s.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
  done
done
for d in "dt1 2700 760 450 140 120 8" "luna 2250 100 450 140 120 6" "hwy 2342 1143 600 160 40 6"; do
  set -- $d
  TS_BIN=$B $R $T/r_$1_s19w.txt --sim $2 $3 $4 --cars $5 --peds $6 --minutes $7 -v
done
echo done > $T/chain19n.done
