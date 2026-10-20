#!/bin/sh
# d11 = d10 + scans along the foot's landing axis + the landing probe under the toes + the descent split: build lm_d11,
# full suites of c10 (lm_t10e), d10 (lm_d10e), d11 at prof 0 / 2, 30 / 60 Hz -> r9/fs_<tag>_p<prof>_<hz>.txt
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
sh sl/bld.sh $L/qs/dtree11 lm_d11 2>&1 | tail -2
sh sl/bld.sh $L/qs/dtree10 lm_d10e 2>&1 | tail -2
sh sl/bld.sh $L/qs/ctree10 lm_t10e 2>&1 | tail -2
for pf in 0 2; do for hz in 30 60; do
  for t in t10e:c10 d10e:d10 d11:d11; do b=${t%%:*}; n=${t##*:}
    LM_HZ=$hz LM_TURNPROF=$pf ./sl/lm_$b > r9/fs_${n}_p${pf}_$hz.txt 2>/dev/null
  done
done; done
echo "job_d11 done $(date +%T)"
