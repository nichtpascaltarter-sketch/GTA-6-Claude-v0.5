#!/bin/sh
# usage: capt.sh <tag> <env...>: the candidate (fkc2 knobs) plus env: stops (4 seeds) and crowd/blocked/SV (8 seeds)
cd /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco/qs
tag=$1; shift
FK="LM_QSHOLD=0.85 LM_QSSNAP=1 LM_DZMOVE=1 LM_STEPFIT=1 LM_SEENWALK=1 LM_STANDSKIP=1 LM_STANDPLANTSTEP=1 LM_DZCLAMP=2"
sh srun2.sh ./lm_qs $tag $FK "$@"
sh qrun2.sh ./lm_qs $tag $FK "$@"
