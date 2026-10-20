#!/bin/bash
# AI agent (set 20): the dead-end turning circles within 2.5 km of the city centre, 13 models from a Kiko to a bus,
# base (set 19 AI, traffic_sim20b) against set 20 (traffic_sim20). Restart-safe; relaunch:
#   AW/ai_detach.sh AW/ts_chain20a.out bash AW/ts_chain20a.sh
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
R=$AW/ts_run.sh
T=$SP/ts
rm -f $T/chain20a.done
once() { local out=$1; shift; grep -q " rc=0" "$out" 2>/dev/null && return 0; "$@"; }
M="Kiko,Falke 7,Sawgrass,Tekko,Palomino,Parcel P70,Lifeline,Packhorse 26,Compactor,Boulevard,Longbow 9,Guardian Pumper,Raijin 1000"
for v in 20b 20; do
  once $T/ut_city_$v.txt env TS_BIN=$T/traffic_sim$v $R $T/ut_city_$v.txt --uturntest 3000 1000 2500 "$M"
done
echo done > $T/chain20a.done
