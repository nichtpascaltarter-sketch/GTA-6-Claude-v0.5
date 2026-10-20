#!/bin/bash
# AI agent (set 19, after the 05:15 container restart - replaces chains n, o, p, killed; restart-safe: every run whose
# output already ends in rc=0 is skipped, so after another restart just launch this again): s19 v5 build (traffic_sim19w;
# the 04:20 one is v4), then v5 against base - seeds without parked cars, parked areas, a second parked seed, Sol Beach
# seeds, the other districts - the car-110 watch on v3, the I-38 gore barrier probe (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19q.done
once() { local out=$1; shift; grep -q " rc=0" "$out" 2>/dev/null && return 0; "$@"; }
if ! grep -q "built" $AW/ts_build19w5.out 2>/dev/null; then
  V=w bash $AW/ts_build19v.sh > $AW/ts_build19w5.out 2>&1
  grep -q built $AW/ts_build19w5.out || { echo "s19w build failed" > $T/chain19q.done; exit 1; }
fi
B=$T/traffic_sim19w
for s in 1 2 3; do
  once $T/q_ramp_s19w_$s.txt env TS_BIN=$B $R $T/q_ramp_s19w_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
  once $T/q_mid_s19w_$s.txt env TS_BIN=$B $R $T/q_mid_s19w_$s.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
done
once $T/p_ramp_s19w.txt env TS_BIN=$B $R $T/p_ramp_s19w.txt --sim 2468 3820 320 --cars 120 --peds 60 --parked 90 --minutes 5 -v
once $T/p_mid_s19w.txt env TS_BIN=$B $R $T/p_mid_s19w.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
once $T/p_dt1_s19w.txt env TS_BIN=$B $R $T/p_dt1_s19w.txt --sim 2700 760 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
for v in base s19w; do
  BB=$T/traffic_sim; [ $v = s19w ] && BB=$B
  once $T/p_mid_${v}_2.txt env TS_BIN=$BB $R $T/p_mid_${v}_2.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 --seed 2 -v
done
for s in 1 2; do
  for v in base s19w; do
    BB=$T/traffic_sim; [ $v = s19w ] && BB=$B
    once $T/q_beach_${v}_$s.txt env TS_BIN=$BB $R $T/q_beach_${v}_$s.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
  done
done
for d in "dt1 2700 760 450 140 120 8" "luna 2250 100 450 140 120 6" "hwy 2342 1143 600 160 40 6"; do
  set -- $d
  once $T/r_$1_s19w.txt env TS_BIN=$B $R $T/r_$1_s19w.txt --sim $2 $3 $4 --cars $5 --peds $6 --minutes $7 -v
done
once $T/w110_mid_s19v.txt env TS_BIN=$T/traffic_sim19v $R $T/w110_mid_s19v.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 3.1 -v --watch 110 150 184
once $T/i38probe.txt env TS_BIN=$T/traffic_sim $R $T/i38probe.txt --colliders 2481 3676 7 --nearlane 2480.1 3681.0 0 -1 --nearlane 2480.1 3681.0 0 1 --lane 68 55 100 --lanes 2482 3690 60 $T/i38_gore.ppm
echo done > $T/chain19q.done
