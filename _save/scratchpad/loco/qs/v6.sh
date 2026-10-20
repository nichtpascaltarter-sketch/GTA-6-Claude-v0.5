#!/bin/sh
# clean6 verification: harness build (gated), stops 8 seeds + crowd/blocked 8 seeds vs gated rs, full suite, anim_test
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L && sh sl/bld.sh $L/qs/ctree6 lm_qsc6 || { echo "build failed"; exit 1; }
cd $L/qs
sh srun2.sh ../sl/lm_qsc6 c6; sh srun3.sh ../sl/lm_qsc6 c6; sh qrun2.sh ../sl/lm_qsc6 c6
n=0
for f in ss_c6_*.txt q_c6_*.txt; do g=$(echo $f | sed 's/_c6_/_rs_/'); d=$(diff <(grep -v '\.land_\|^time\|^animator' $f) <(grep -v '\.land_\|^time\|^animator' $g) | wc -l); [ $d != 0 ] && { echo "DIFF $f $d"; n=$((n+1)); }; done
echo "compare done: $n differing files"
for hz in 30 60; do LM_HZ=$hz ../sl/lm_qsc6 > fz_c6_$hz.txt 2>/dev/null & done; wait
for hz in 30 60; do echo "== full suite $hz (c4 -> c6) changed:"; diff fz_c4_$hz.txt fz_c6_$hz.txt | grep '^[<>] M' | awk '{print $3}' | sed 's/\.[a-z_0-9]*$//' | sort | uniq -c | tr '\n' ' '; echo; done
sh at7.sh
echo "v6 done $(date +%T)"
