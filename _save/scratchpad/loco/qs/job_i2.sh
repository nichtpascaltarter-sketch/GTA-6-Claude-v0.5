#!/bin/sh
# rebuild lm_t9i / lm_t10 with the sole-clearance metric; interior + straight stairs runs for both (prof 0 and 2, 30/60 Hz)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L && sh sl/bld.sh $L/qs/ctree9 lm_t9i 2>&1 | tail -1 && sh sl/bld.sh $L/qs/ctree10 lm_t10 2>&1 | tail -1
rm -f r9/int2_*
for pf in 0 2; do for hz in 30 60; do
  for f in switchback lstair spiral room stairs; do
    LM_HZ=$hz LM_TURNPROF=$pf ./sl/lm_t9i $f >> r9/int2_c9_p${pf}_$hz.txt 2>/dev/null
    LM_HZ=$hz LM_TURNPROF=$pf ./sl/lm_t10 $f >> r9/int2_c10_p${pf}_$hz.txt 2>/dev/null
  done
done; done
echo "job_i2 done $(date +%T)"
