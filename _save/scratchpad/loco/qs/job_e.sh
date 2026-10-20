#!/bin/bash
# job_e.sh: c13b's harness numbers for its sign-off (sl/lm_c13 = c13b since 05:48): vm_c13b (vmat), player speeds
# pl_lm_c13b_*, run paces run_c13b_* (as made for c13)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
sh qs/vmat.sh lm_c13 c13b
for v in 1.55 4.1 6.9; do for hz in 60 30; do pr=$([ $hz = 60 ] && echo 2 || echo 0)
  LM_SV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_c13 stairs > r9/pl_lm_c13b_s_${v}_$hz.txt 2>/dev/null
  LM_IREP=4 LM_IV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_c13 switchback > r9/pl_lm_c13b_sb_${v}_$hz.txt 2>/dev/null
done; done
for v in 3.0 5.0 7.0; do
  LM_SV=$v LM_HZ=60 LM_TURNPROF=2 ./sl/lm_c13 stairs > r9/run_c13b_$v.txt 2>/dev/null
  LM_SV=$v LM_HZ=30 LM_TURNPROF=0 ./sl/lm_c13 stairs > r9/run_c13b_${v}_30.txt 2>/dev/null
done
n=0; for f in r9/vm_c13b_* r9/pl_lm_c13b_* r9/run_c13b_*; do g=$(echo $f | sed 's/c13b_/c13_/'); cmp -s <(grep "^M" $f) <(grep "^M" $g) || { n=$((n+1)); echo "differs: $f"; }; done
echo "c13b vs c13 (vm / pl / run): differing files $n"
echo "job_e done"
