#!/bin/bash
# job_h.sh: the late re-scan's keep (SW) only for a tread UP from the ground the foot left (LM_FIXSWUP=1) or while the
# root rises (=2): SW made a 15 cm float at a stop going down a straight flight (stairs17.stops.down float max 43 -> 150
# mm, fs p2_60). Rebuild lm_d15, then evalv2 c14u1 / c14u2 (B14 + SWW + SWUP) and the stops case.
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
while kill -0 16795 2>/dev/null && ! grep -q "lm_c15d vs debug" qs/job_g.log; do sleep 30; done
B14="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5 LM_FIXSW=0.15 LM_FIXSEEN=1 LM_FIXSWW=1"
sh mk2.sh qs/dtree15 lm_d15
env $B14 LM_HZ=30 LM_TURNPROF=0 ./sl/lm_d15 > r9/chk_swup_default.txt 2>/dev/null
cmp -s <(grep "^M" r9/chk_swup_default.txt) <(grep "^M" r9/ev2_c14w_fs_p0_30.txt) && echo "default unchanged" || echo "DEFAULT CHANGED"
for u in 1 2; do
  sh qs/evalv2.sh lm_d15 c14u$u $B14 LM_FIXSWUP=$u >/dev/null 2>&1
  echo "c14u$u: $(grep -h 'stairs17.stops.down.planted_float_max_mm' r9/ev2_c14u${u}_fs_p2_60.txt)"
done
echo "job_h done"
