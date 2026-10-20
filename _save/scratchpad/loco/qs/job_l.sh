#!/bin/bash
# job_l.sh: SW's keep at a steady pace only (LM_FIXSWST=1: x blendSteady; the stop float came from a keep while the
# walker was stopping, steady 0.25 -> 0.14) on c14c's switches -> c14st; and SEEN alone on c13b -> c13se (is c14 = SEEN
# alone the clean part?)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
while ! grep -q "job_k done" qs/job_k.log 2>/dev/null; do sleep 30; done
B13="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5"
B14="$B13 LM_FIXSW=0.15 LM_FIXSEEN=1 LM_FIXSWW=1"
sh mk2.sh qs/dtree15 lm_d15
env $B14 LM_HZ=30 LM_TURNPROF=0 ./sl/lm_d15 > r9/chk_swst_default.txt 2>/dev/null
cmp -s <(grep "^M" r9/chk_swst_default.txt) <(grep "^M" r9/ev2_c14w_fs_p0_30.txt) && echo "default unchanged" || echo "DEFAULT CHANGED"
sh qs/evalv2.sh lm_d15 c14st $B14 LM_FIXSWST=1 >/dev/null 2>&1
sh qs/evalv2.sh lm_d15 c13se $B13 LM_FIXSEEN=1 >/dev/null 2>&1
for t in c13b c13se c14w c14st; do echo "$t: $(grep -h 'stairs17.stops.*float_max' r9/ev2_${t}_fs_p2_60.txt r9/ev2_${t}_fs_p0_30.txt | awk '{printf "%s ", $3}')"; done
echo "job_l done"
