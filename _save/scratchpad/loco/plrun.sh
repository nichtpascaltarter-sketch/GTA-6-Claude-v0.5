#!/bin/sh
# plrun.sh <bin>: the player-speed harness (straight stairs LM_SV, switchback LM_IV x4) at 1.55 / 4.1 / 6.9, 60 Hz (turn
# profile 2) and 30 Hz (0) into r9/pl_<bin>_{s,sb}_<v>_<hz>.txt (plsum.py reads them); no walls
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
for v in 1.55 4.1 6.9; do for hz in 60 30; do pr=$([ $hz = 60 ] && echo 2 || echo 0)
  LM_SV=$v LM_HZ=$hz LM_TURNPROF=$pr nice -n 4 ./sl/$1 stairs > r9/pl_$1_s_${v}_$hz.txt 2>/dev/null
  LM_IREP=4 LM_IV=$v LM_HZ=$hz LM_TURNPROF=$pr nice -n 4 ./sl/$1 switchback > r9/pl_$1_sb_${v}_$hz.txt 2>/dev/null
done; done
