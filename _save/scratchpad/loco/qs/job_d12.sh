#!/bin/sh
# rebuild the harness binaries (flat strolls 0.5 / 0.7, swing correction) for c10, d11, d12; then the full suites
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
sh sl/bld.sh $L/qs/dtree12 lm_d12 2>&1 | tail -1
sh sl/bld.sh $L/qs/dtree11 lm_d11 2>&1 | tail -1
sh sl/bld.sh $L/qs/ctree10 lm_t10e 2>&1 | tail -1
for pf in 0 2; do for hz in 30 60; do
  for t in t10e:c10 d11:d11 d12:d12; do b=${t%%:*}; n=${t##*:}
    LM_HZ=$hz LM_TURNPROF=$pf ./sl/lm_$b > r9/fs_${n}_p${pf}_$hz.txt 2>/dev/null
  done
done; done
echo "job_d12 done $(date +%T)"
