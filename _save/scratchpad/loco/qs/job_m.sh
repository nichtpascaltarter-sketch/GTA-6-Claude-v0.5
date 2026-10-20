#!/bin/bash
# job_m.sh: after job_l, the curves round's next version c15e = c14c's switches + WB2 only going up (LM_FIXWBUP: the
# tight switchback descent's 17.9 cm sink came from WB2 fitting a too-short run two treads down) + the lateral shift
# with a minimum bend curvature 0.5 /m (LM_SPLATMIN=0.5: the straight crowd descent's regression came from the shift)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
while ! grep -q "job_l done" qs/job_l.log 2>/dev/null; do sleep 30; done
B14="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5 LM_FIXSW=0.15 LM_FIXSEEN=1 LM_FIXSWW=1"
E="$B14 LM_FIXWB=2 LM_FIXWBUP=1 LM_SPLAT=3 LM_SPLATDN=1 LM_SPLATS=0.6 LM_SPLATMIN=0.5"
sh qs/evalv2.sh lm_d15 c15e $E >/dev/null 2>&1
for fam in spiral winder curved; do
  env $E LM_KIT=1 LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 ./sl/lm_d15 kit.$fam > r9/kit${fam}_c15e_iv5.txt 2>/dev/null
  env $E LM_KIT=1 LM_HZ=60 LM_TURNPROF=2 ./sl/lm_d15 kit.$fam > r9/kit${fam}_c15e_pace.txt 2>/dev/null
done
echo "c15e: $(grep -h 'stairs17.crowd.down.planted_sunk_3cm_pct\|switchback_tight.down_1.0.lowest_sole_mm' r9/ev2_c15e_fs_p2_60.txt | awk '{printf "%s=%s ", $2, $3}')"
echo "job_m done"
