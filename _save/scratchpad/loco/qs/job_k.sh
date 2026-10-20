#!/bin/bash
# job_k.sh: after job_j, the curves round's lateral shift with LM_SPLATMINR (curvature from min(|smoothed|, |present|)
# turn rate): c15d showed a 17.9 cm sink on switchback_tight.down (fs p2_60) and stairs17.crowd.down sunk3 1.44 -> 2.08
# -> c15m = B15 (as c15ddbg) + LM_SPLATMINR=1
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
while ! grep -q "job_j done" qs/job_j.log 2>/dev/null; do sleep 30; done
B15="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5 LM_FIXSW=0.15 LM_FIXSEEN=1 LM_FIXSWW=1 LM_FIXWB=2 LM_SPLAT=3 LM_SPLATDN=1 LM_SPLATS=0.6"
sh qs/evalv2.sh lm_d15 c15m $B15 LM_SPLATMINR=1 >/dev/null 2>&1
for fam in spiral winder curved; do
  env $B15 LM_SPLATMINR=1 LM_KIT=1 LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 ./sl/lm_d15 kit.$fam > r9/kit${fam}_c15m_iv5.txt 2>/dev/null
  env $B15 LM_SPLATMINR=1 LM_KIT=1 LM_HZ=60 LM_TURNPROF=2 ./sl/lm_d15 kit.$fam > r9/kit${fam}_c15m_pace.txt 2>/dev/null
done
for t in c15ddbg c15m; do echo "$t: $(grep -h 'switchback_tight.down_1.0.lowest_sole_mm\|stairs17.crowd.down.planted_sunk_3cm_pct' r9/ev2_${t}_fs_p2_60.txt | awk '{printf "%s=%s ", $2, $3}')"; done
echo "job_k done"
