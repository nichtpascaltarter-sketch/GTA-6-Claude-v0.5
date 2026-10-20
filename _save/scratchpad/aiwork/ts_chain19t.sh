#!/bin/bash
# AI agent (set 19): s19 v7 = v6 + the all-way stop right after a box (stopped at its line with the tail still in the box
# before, the car makes its stop and its turn in that stop's order there: Sol Beach nodes 2546 / 2544 held a left turner
# for good) as traffic_sim19y; the two Sol Beach seeds, then a watch of the Calle Luna car that left the road (v6).
# Restart-safe: runs whose output ends in rc=0 are skipped - after a restart relaunch it:
#   AW/ai_detach.sh AW/ts_chain19t.out bash AW/ts_chain19t.sh   (one harness job at a time)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19t.done
once() { local out=$1; shift; grep -q " rc=0" "$out" 2>/dev/null && return 0; "$@"; }
if ! grep -q "built" $AW/ts_build19y.out 2>/dev/null; then
  V=y bash $AW/ts_build19v.sh > $AW/ts_build19y.out 2>&1
  grep -q built $AW/ts_build19y.out || { echo "s19y build failed" > $T/chain19t.done; exit 1; }
fi
B=$T/traffic_sim19y
for s in 1 2; do
  once $T/q_beach_s19y_$s.txt env TS_BIN=$B $R $T/q_beach_s19y_$s.txt --sim 5250 600 450 --cars 140 --peds 120 --minutes 6 --seed $s -v
done
once $T/w53_luna_s19x.txt env TS_BIN=$T/traffic_sim19x $R $T/w53_luna_s19x.txt --sim 2250 100 450 --cars 140 --peds 120 --minutes 5.9 -v --watch 53 285 353
echo done > $T/chain19t.done
