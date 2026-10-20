#!/bin/sh
# usage: sbw.sh <bin> [g] : the switchback suites with walls at 1.55 / 4.1 / 6.9 m/s into r11/sbw1_<bin>_<v>.txt (and
# without walls: sbw0); g: the game's route and player turning (LM_SBGAME) into sbg1 / sbg0
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
for v in 1.55 4.1 6.9; do
  if [ "$2" = g ]; then
    LM_SBGAME=1 LM_SBWALLS=1 LM_IREP=4 LM_IV=$v LM_HZ=60 ./sl/lm_$1 switchback > r11/sbg1_$1_$v.txt 2>/dev/null
    LM_SBGAME=1 LM_IREP=4 LM_IV=$v LM_HZ=60 ./sl/lm_$1 switchback > r11/sbg0_$1_$v.txt 2>/dev/null
  else
    LM_SBWALLS=1 LM_IREP=4 LM_IV=$v LM_HZ=60 LM_TURNPROF=2 ./sl/lm_$1 switchback > r11/sbw1_$1_$v.txt 2>/dev/null
    LM_IREP=4 LM_IV=$v LM_HZ=60 LM_TURNPROF=2 ./sl/lm_$1 switchback > r11/sbw0_$1_$v.txt 2>/dev/null
  fi
done
