#!/bin/sh
# anim_test on attree11 (pivot patch), then the interior scenarios for c9 (lm_t9i, rebuilt with the interior harness)
# and c10 (lm_t10), both profiles and rates
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
sh $L/qs/at11.sh
cd $L && sh sl/bld.sh $L/qs/ctree9 lm_t9i 2>&1 | tail -1
for pf in 0 1 2; do for hz in 30 60; do
  for f in switchback lstair spiral room; do
    LM_HZ=$hz LM_TURNPROF=$pf ./sl/lm_t9i $f >> r9/int_c9_p${pf}_$hz.txt 2>/dev/null
    LM_HZ=$hz LM_TURNPROF=$pf ./sl/lm_t10 $f >> r9/int_c10_p${pf}_$hz.txt 2>/dev/null
  done
done; done
echo "interior runs done $(date +%T)"
