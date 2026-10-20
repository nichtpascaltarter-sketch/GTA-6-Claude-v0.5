#!/bin/bash
# job_n.sh: c14d (c14c + the late keep at a steady pace only: x blendSteady) -> sl/lm_c14d checked against the debug
# c14st (B14 + SWW + SWST), player speeds pl_lm_c14d_*
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
sh mk2.sh qs/ctree14d lm_c14d
sh qs/evalv2.sh lm_c14d c14d >/dev/null 2>&1
n=0; for f in r9/ev2_c14d_*.txt; do g=$(echo $f | sed "s/ev2_c14d_/ev2_c14st_/"); cmp -s <(grep "^M" $f) <(grep "^M" $g) || n=$((n+1)); done
echo "lm_c14d vs debug c14st: differing files $n"
for v in 1.55 4.1 6.9; do for hz in 60 30; do pr=$([ $hz = 60 ] && echo 2 || echo 0)
  LM_SV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_c14d stairs > r9/pl_lm_c14d_s_${v}_$hz.txt 2>/dev/null
  LM_IREP=4 LM_IV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_c14d switchback > r9/pl_lm_c14d_sb_${v}_$hz.txt 2>/dev/null
done; done
echo "job_n done"
