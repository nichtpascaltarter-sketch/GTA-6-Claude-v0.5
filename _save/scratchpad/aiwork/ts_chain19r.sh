#!/bin/bash
# AI agent (set 19): the stuck spot first (cars stuck at 3636 2851, lane 24167, in both v3 and v5 parked Midtown runs,
# throttle on, nothing within 4.5 m, no wall), then the rest of chain q (which skips what is done). Waits for chain q's
# running job, then stops chain q (one harness job at a time).
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain19r.done
for p in $(ps -eo pid,args | awk '$2 == "bash" && $3 ~ /ts_chain19q\.sh$/ {print $1}'); do kill $p; done
while [ -n "$(ps -eo args | awk '$1 ~ /scratchpad\/ts\/traffic_sim/ {print}')" ]; do sleep 10; done
once() { local out=$1; shift; grep -q " rc=0" "$out" 2>/dev/null && return 0; "$@"; }
once $T/w71_mid_s19w.txt env TS_BIN=$T/traffic_sim19w $R $T/w71_mid_s19w.txt --sim 3600 3200 450 --cars 140 --peds 120 --parked 140 --minutes 5.45 -v --watch 71 295 327
bash $AW/ts_chain19q.sh
echo done > $T/chain19r.done
