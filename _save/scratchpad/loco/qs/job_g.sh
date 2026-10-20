#!/bin/bash
# job_g.sh: c14c (c14b + the late re-scan's keep at a walk only) and c15d (c14c + curves + curves2: WB2, the inner foot's
# shift going down round a steady bend, stairLat resets): harness builds sl/lm_c14c, sl/lm_c15d, each checked against
# lm_d15 + switches (c14w = B14 + LM_FIXSWW=1 from job_f; c15ddbg = B15 + LM_SPLATS=0.6 + LM_FIXSWW=1), player speeds
# for c14c, kit sweeps (spiral / winder / curved, 0.5 and the pace) for c14c and c15d
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
B14="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5 LM_FIXSW=0.15 LM_FIXSEEN=1 LM_FIXSWW=1"
B15="$B14 LM_FIXWB=2 LM_SPLAT=3 LM_SPLATDN=1 LM_SPLATS=0.6"
sh mk2.sh qs/ctree14c lm_c14c
sh mk2.sh qs/ctree15d lm_c15d
chk() {   # chk <bin> <tag> <dbgtag> <switches...>
  b=$1; t=$2; dt=$3; shift 3
  [ -f r9/ev2_${dt}_fs_p0_30.txt ] || sh qs/evalv2.sh lm_d15 $dt "$@" >/dev/null 2>&1
  sh qs/evalv2.sh $b $t >/dev/null 2>&1
  n=0; for f in r9/ev2_${t}_*.txt; do g=$(echo $f | sed "s/ev2_${t}_/ev2_${dt}_/"); cmp -s <(grep "^M" $f) <(grep "^M" $g) || n=$((n+1)); done
  echo "$b vs debug $dt: differing files $n"
}
chk lm_c14c c14c c14w $B14
chk lm_c15d c15d c15ddbg $B15
for v in 1.55 4.1 6.9; do for hz in 60 30; do pr=$([ $hz = 60 ] && echo 2 || echo 0)
  LM_SV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_c14c stairs > r9/pl_lm_c14c_s_${v}_$hz.txt 2>/dev/null
  LM_IREP=4 LM_IV=$v LM_HZ=$hz LM_TURNPROF=$pr ./sl/lm_c14c switchback > r9/pl_lm_c14c_sb_${v}_$hz.txt 2>/dev/null
done; done
echo "player speeds done"
for b in c14c c15d; do for fam in spiral winder curved; do
  LM_KIT=1 LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 ./sl/lm_$b kit.$fam > r9/kit${fam}_${b}_iv5.txt 2>/dev/null
  LM_KIT=1 LM_HZ=60 LM_TURNPROF=2 ./sl/lm_$b kit.$fam > r9/kit${fam}_${b}_pace.txt 2>/dev/null
done; done
echo "job_g done"
