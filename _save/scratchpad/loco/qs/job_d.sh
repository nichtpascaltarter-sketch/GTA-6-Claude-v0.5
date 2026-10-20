#!/bin/bash
# job_d.sh: after job_c, c15c (c15b + the lateral shift on a steady bend: turn rate smoothed 0.6 s; stairLat reset)
# -> sl/lm_c15c, checked against lm_d15 + its switches (r9/ev2_c15cdbg_* vs r9/ev2_c15c_*)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
while ! grep -q "job_c done" qs/job_c.log 2>/dev/null; do sleep 30; done
B15="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5 LM_FIXSW=0.15 LM_FIXSEEN=1 LM_FIXWB=2 LM_SPLAT=3 LM_SPLATDN=1 LM_SPLATS=0.6"
sh mk2.sh qs/ctree15c lm_c15c
[ -f r9/ev2_c15cdbg_fs_p0_30.txt ] || sh qs/evalv2.sh lm_d15 c15cdbg $B15 >/dev/null 2>&1
sh qs/evalv2.sh lm_c15c c15c >/dev/null 2>&1
n=0; for f in r9/ev2_c15c_*.txt; do g=$(echo $f | sed "s/ev2_c15c_/ev2_c15cdbg_/"); cmp -s <(grep "^M" $f) <(grep "^M" $g) || n=$((n+1)); done
echo "lm_c15c vs debug c15cdbg: differing files $n"
echo "job_d done"
