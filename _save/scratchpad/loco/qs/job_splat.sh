#!/bin/sh
# job_splat.sh: after the debug build (lm_d15 with LM_SPLAT), the lateral shift on top of c14: k 3 / 6, descents only
# (LM_SPLATDN=1) and both ways -> r9/ev2_c14sp<k>[dn]_* and kit sweeps of spirals / winders / curved at 0.5 and the pace
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
C="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXSW=0.15 LM_FIXSEEN=1"
for v in "3 1" "6 1" "3 0" "6 0"; do
  set -- $v; k=$1; dn=$2; tag=c14sp${k}$([ $dn = 1 ] && echo dn)
  E="$C LM_SPLAT=$k"; [ $dn = 1 ] && E="$E LM_SPLATDN=1"
  sh qs/evalv2.sh lm_d15 $tag $E >/dev/null 2>&1
  for fam in spiral winder curved; do
    env $E LM_KIT=1 LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/lm_d15 kit.$fam > r9/kit${fam}_${tag}_iv5.txt 2>/dev/null
    env $E LM_KIT=1 LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/lm_d15 kit.$fam > r9/kit${fam}_${tag}_pace.txt 2>/dev/null
  done
  echo "$tag done $(date -u +%T)"
done
for fam in spiral winder curved; do
  LM_KIT=1 LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/lm_c14 kit.$fam > r9/kit${fam}_c14_iv5.txt 2>/dev/null
  LM_KIT=1 LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/lm_c14 kit.$fam > r9/kit${fam}_c14_pace.txt 2>/dev/null
done
echo "job_splat done"
