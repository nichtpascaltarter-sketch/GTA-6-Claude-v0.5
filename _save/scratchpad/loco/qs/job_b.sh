#!/bin/bash
# job_b.sh: the c13b / c14b / c15b harness builds (the early-swing limit on the stairs hand-over), each checked against
# the debug build's switches, and c13b's anim_test (attree14 with c13b)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
B13="LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5"
B14="$B13 LM_FIXSW=0.15 LM_FIXSEEN=1"
B15="$B14 LM_FIXWB=2 LM_SPLAT=3 LM_SPLATDN=1"
chk() {   # chk <bin> <tag> <dbgtag> <switches...>
  b=$1; t=$2; dt=$3; shift 3
  [ -f r9/ev2_${dt}_fs_p0_30.txt ] || sh qs/evalv2.sh lm_d15 $dt "$@" >/dev/null 2>&1
  sh qs/evalv2.sh $b $t >/dev/null 2>&1
  n=0; for f in r9/ev2_${t}_*.txt; do g=$(echo $f | sed "s/ev2_${t}_/ev2_${dt}_/"); cmp -s <(grep "^M" $f) <(grep "^M" $g) || n=$((n+1)); done
  echo "$b vs debug $dt: differing files $n"
}
for v in 13 14 15; do sh mk2.sh qs/ctree$v lm_c$v; done; ls -la --time-style=+%H:%M sl/lm_c13 sl/lm_c14 sl/lm_c15
chk lm_c13 c13bc c13b $B13
chk lm_c14 c14bc c14bdbg $B14
chk lm_c15 c15bc c15bdbg $B15
cp qs/clean13b/animator_qs13b.cpp attree14/src/anim/animator.cpp
sh qs/at14.sh
mv sl/at_qs13.log sl/at_qs13b.log
echo "job_b done"
