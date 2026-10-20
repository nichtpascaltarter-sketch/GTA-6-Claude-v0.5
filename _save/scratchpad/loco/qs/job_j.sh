#!/bin/bash
# job_j.sh: after job_i, LM_FIXATB=<m> (a tread fit with a nose behind it holds only while the ball is at least m past
# that nose: a walker stopping on a flight going up came down 18-25 cm short of a fit two treads up and stood 15-33 cm
# in the air - in HEAD, c12, c13 too: stairs17.stops.up float max 161-166 mm). On c13b (B13) and on c14c (B14 + SWW):
# c13ab0 / c13ab2 / c14ab2
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
while ! grep -q "job_i done" qs/job_i.log 2>/dev/null; do sleep 30; done
B13="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5"
B14="$B13 LM_FIXSW=0.15 LM_FIXSEEN=1 LM_FIXSWW=1"
sh qs/evalv2.sh lm_d15 c13ab0 $B13 LM_FIXATB=0 >/dev/null 2>&1
sh qs/evalv2.sh lm_d15 c13ab2 $B13 LM_FIXATB=0.02 >/dev/null 2>&1
sh qs/evalv2.sh lm_d15 c14ab2 $B14 LM_FIXATB=0.02 >/dev/null 2>&1
for t in c13b c13ab0 c13ab2 c14w c14ab2; do echo "$t: $(grep -h 'stairs17.stops.*float_max' r9/ev2_${t}_fs_p2_60.txt r9/ev2_${t}_fs_p0_30.txt | awk '{printf "%s ", $3}')"; done
echo "job_j done"
