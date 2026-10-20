#!/bin/bash
# job_i.sh: after job_h, LM_FIXSWMAX=<m> (the late re-scan's keep as a bonus for the kept tread only while it needs a
# shift of at most m: a walker stopping on a flight kept a fit two treads up 13-18 cm ahead and stood 15-33 cm in the
# air): c14m08 / c14m10 / c14m12 = B14 + SWW + SWMAX
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
while ! grep -q "job_h done" qs/job_h.log 2>/dev/null; do sleep 30; done
B14="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5 LM_FIXSW=0.15 LM_FIXSEEN=1 LM_FIXSWW=1"
for m in 0.08 0.10 0.12; do
  t=c14m$(echo $m | sed 's/0\.//')
  sh qs/evalv2.sh lm_d15 $t $B14 LM_FIXSWMAX=$m >/dev/null 2>&1
  echo "$t: $(grep -h 'stairs17.stops.down.planted_float_max_mm\|stairs17.stops.up.planted_float_max_mm' r9/ev2_${t}_fs_p2_60.txt | awk '{printf "%s=%s ", $2, $3}')"
done
echo "job_i done"
