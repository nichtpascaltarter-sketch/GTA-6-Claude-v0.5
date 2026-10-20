#!/bin/bash
# AI agent (set 20): the meeting test (two wide vehicles on edge 2907's S-bend) for set 19 (20b), the crawl only (20c) and
# crawl + bend wait (20); then two buses on it. Restart-safe; relaunch: AW/ai_detach.sh AW/ts_chain20m.out bash AW/ts_chain20m.sh
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
T=$SP/ts
rm -f $T/chain20m.done
for v in "s19 b" "s20c c" "s20 x"; do
  set -- $v
  [ -x $T/traffic_sim20$2 ] && [ $T/traffic_sim20$2 -nt $AW/s20/traffic_sim.cpp ] && [ $T/traffic_sim20$2 -nt $AW/$1/traffic_core.cpp ] || SRC=$1 V=$2 bash $AW/ts_build20.sh > $AW/ts_build20$2.out 2>&1
done
cd $T/tree
for v in b c x; do
  for pair in "Guardian Pumper|Packhorse 26" "Boulevard|Boulevard"; do
    A=${pair%|*}; B=${pair#*|}; tag=$(echo "$A" | cut -c1-4)
    out=$T/mt_2907_${v}_${tag}.txt
    grep -q "^meet test:.*meeting points" $out 2>/dev/null && continue
    $AW/ai_memgate.sh 3000
    nice -n 5 timeout 1800 $T/traffic_sim20$v --meettest 2907 "$A" "$B" 2>&1 | grep -E "meet test|meeting point" > $out
  done
done
echo done > $T/chain20m.done
