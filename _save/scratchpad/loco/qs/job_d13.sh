#!/bin/sh
# job_d13: lm_d13 (plant-time ball probe overNose; LM_BALLFWD) vs the candidate: vmat + kit sweeps
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
kit() {   # kit <tag> [ENV...]
  t=$1; shift
  env "$@" LM_KIT=1 LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/lm_d13 kit. > r9/kit_${t}_iv5.txt 2>/dev/null
  env "$@" LM_KIT=1 LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/lm_d13 kit. > r9/kit_${t}_pace.txt 2>/dev/null
}
echo "start $(date +%T)"
env LM_PLANTBALL0=1 LM_KIT=1 LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 ./sl/lm_d13 kit.straight > r9/kit_d13pb0_chk.txt 2>/dev/null
echo "chk $(date +%T)"
nice -n 4 sh qs/vmat.sh lm_d13 d13pb0 LM_PLANTBALL0=1
echo "vm d13pb0 $(date +%T)"
nice -n 4 sh qs/vmat.sh lm_d13 d13
echo "vm d13 $(date +%T)"
nice -n 4 sh qs/vmat.sh lm_d13 d13f3 LM_BALLFWD=3
echo "vm d13f3 $(date +%T)"
kit d13
echo "kit d13 $(date +%T)"
kit d13f3 LM_BALLFWD=3
echo "kit d13f3 $(date +%T)"
echo "job_d13 done"
