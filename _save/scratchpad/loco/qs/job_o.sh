#!/bin/bash
# job_o.sh: c15f = c14d + curves + curves2 + curves3 (WB2 going up only; the lateral shift on a steady bend of 0.5 /m or
# more) -> sl/lm_c15f checked against the debug c15fdbg (c14d's switches + WB2 + WBUP + SPLAT 3 dn S0.6 MIN0.5), kits
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
while ! grep -q "job_n done" qs/job_n.log 2>/dev/null; do sleep 30; done
E="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5 LM_FIXSW=0.15 LM_FIXSEEN=1 LM_FIXSWW=1 LM_FIXSWST=1 LM_FIXWB=2 LM_FIXWBUP=1 LM_SPLAT=3 LM_SPLATDN=1 LM_SPLATS=0.6 LM_SPLATMIN=0.5"
sh mk2.sh qs/ctree15f lm_c15f
sh qs/evalv2.sh lm_d15 c15fdbg $E >/dev/null 2>&1
sh qs/evalv2.sh lm_c15f c15f >/dev/null 2>&1
n=0; for f in r9/ev2_c15f_*.txt; do g=$(echo $f | sed "s/ev2_c15f_/ev2_c15fdbg_/"); cmp -s <(grep "^M" $f) <(grep "^M" $g) || n=$((n+1)); done
echo "lm_c15f vs debug c15fdbg: differing files $n"
for b in c14d c15f; do for fam in spiral winder curved; do
  LM_KIT=1 LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 ./sl/lm_$b kit.$fam > r9/kit${fam}_${b}_iv5.txt 2>/dev/null
  LM_KIT=1 LM_HZ=60 LM_TURNPROF=2 ./sl/lm_$b kit.$fam > r9/kit${fam}_${b}_pace.txt 2>/dev/null
done; done
for v in 1.55 4.1 6.9; do for hz in 60 30; do pr=$([ $hz = 60 ] && echo 2 || echo 0)
  LM_SV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_c15f stairs > r9/pl_lm_c15f_s_${v}_$hz.txt 2>/dev/null
  LM_IREP=4 LM_IV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_c15f switchback > r9/pl_lm_c15f_sb_${v}_$hz.txt 2>/dev/null
done; done
echo "job_o done"
