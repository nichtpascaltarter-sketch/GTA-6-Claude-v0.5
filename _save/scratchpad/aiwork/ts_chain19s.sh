#!/bin/bash
# AI agent (set 19): s19 v6 = v5 + a fixed 0.25 m corridor margin for still parked cars (v5's speed-dependent margin made
# a limit cycle by a van parked 0.3 m into reach: brake / pull away twice a second, taken for stuck) as traffic_sim19x,
# then the whole comparison against base. Restart-safe: runs whose output ends in rc=0 are skipped - after a restart
# relaunch it: AW/ai_detach.sh AW/ts_chain19s.out bash AW/ts_chain19s.sh (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19s.done
once() { local out=$1; shift; grep -q " rc=0" "$out" 2>/dev/null && return 0; "$@"; }
if ! grep -q "built" $AW/ts_build19x.out 2>/dev/null; then
  V=x bash $AW/ts_build19v.sh > $AW/ts_build19x.out 2>&1
  grep -q built $AW/ts_build19x.out || { echo "s19x build failed" > $T/chain19s.done; exit 1; }
fi
B=$T/traffic_sim19x
once $T/p_mid_s19x.txt env TS_BIN=$B $R $T/p_mid_s19x.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
once $T/p_ramp_s19x.txt env TS_BIN=$B $R $T/p_ramp_s19x.txt --sim 2468 3820 320 --cars 120 --peds 60 --parked 90 --minutes 5 -v
once $T/p_dt1_s19x.txt env TS_BIN=$B $R $T/p_dt1_s19x.txt --sim 2700 760 450 --cars 140 --peds 120 --parked 140 --minutes 6 -v
for v in base s19x; do
  BB=$T/traffic_sim; [ $v = s19x ] && BB=$B
  once $T/p_mid_${v}_2.txt env TS_BIN=$BB $R $T/p_mid_${v}_2.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 6 --seed 2 -v
done
for s in 1 2 3; do
  once $T/q_ramp_s19x_$s.txt env TS_BIN=$B $R $T/q_ramp_s19x_$s.txt --sim 2468 3820 320 --cars 120 --peds 60 --minutes 5 --seed $s -v
  once $T/q_mid_s19x_$s.txt env TS_BIN=$B $R $T/q_mid_s19x_$s.txt --sim 3600 3200 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
done
for s in 1 2; do
  for v in base s19x; do
    BB=$T/traffic_sim; [ $v = s19x ] && BB=$B
    once $T/q_beach_${v}_$s.txt env TS_BIN=$BB $R $T/q_beach_${v}_$s.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
  done
done
for d in "dt1 2700 760 450 140 120 8" "luna 2250 100 450 140 120 6" "hwy 2342 1143 600 160 40 6"; do
  set -- $d
  once $T/r_$1_s19x.txt env TS_BIN=$B $R $T/r_$1_s19x.txt --sim $2 $3 $4 --cars $5 --peds $6 --minutes $7 -v
done
once $T/i38probe.txt env TS_BIN=$T/traffic_sim $R $T/i38probe.txt --colliders 2481 3676 7 --nearlane 2480.1 3681.0 0 -1 --nearlane 2480.1 3681.0 0 1 --lane 68 55 100 --lanes 2482 3690 60 $T/i38_gore.ppm
echo done > $T/chain19s.done
