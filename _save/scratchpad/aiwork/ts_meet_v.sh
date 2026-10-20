#!/bin/bash
# AI agent (set 20): the meeting test on edge 2907 for one harness variant: V=x ts_meet_v.sh TAG  -> SP/ts/mt_2907_TAG_{Guar,Boul}.txt
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
T=$SP/ts
V=${V:-x}
TAG=${1:-$V}
EDGE=${EDGE:-2907}
cd $T/tree
for pair in "Guardian Pumper|Packhorse 26" "Boulevard|Boulevard"; do
  A=${pair%|*}; B=${pair#*|}; tag=$(echo "$A" | cut -c1-4)
  out=$T/mt_${EDGE}_${TAG}_${tag}.txt
  $AW/ai_memgate.sh 3000
  nice -n 5 timeout 1800 $T/traffic_sim20$V --meettest $EDGE "$A" "$B" 2>&1 | grep -E "meet test|meeting point" > $out
done
echo done > $T/meet_${TAG}.done
