#!/bin/sh
# interior v2 (code-typical stairs): build lm_t9i (c9), lm_t10 (pivot), lm_d10 (pivot + curved landing scans); run the
# interior scenarios for each, prof 0 and 2, 30 / 60 Hz -> r9/int4_<tag>_p<prof>_<hz>.txt
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
until sh $L/prio_ok.sh; do sleep 20; done
sh sl/bld.sh $L/qs/ctree9 lm_t9i 2>&1 | tail -2
sh sl/bld.sh $L/qs/ctree10 lm_t10 2>&1 | tail -2
sh sl/bld.sh $L/qs/dtree10 lm_d10 2>&1 | tail -2
rm -f r9/int4_*
for pf in 0 2; do for hz in 30 60; do
  for f in switchback lstair winder spiral curved room; do
    LM_HZ=$hz LM_TURNPROF=$pf ./sl/lm_t9i $f >> r9/int4_c9_p${pf}_$hz.txt 2>/dev/null
    LM_HZ=$hz LM_TURNPROF=$pf ./sl/lm_t10 $f >> r9/int4_c10_p${pf}_$hz.txt 2>/dev/null
    LM_HZ=$hz LM_TURNPROF=$pf ./sl/lm_d10 $f >> r9/int4_d10_p${pf}_$hz.txt 2>/dev/null
  done
done; done
echo "job_i4 done $(date +%T)"
