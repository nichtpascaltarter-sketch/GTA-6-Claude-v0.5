#!/bin/sh
# vmat.sh <binary> <tag> [ENV=1 ...]: full suites (p0_30, p2_60) and the interior at the scenario pace and at 0.5 m/s
# (p2_60) -> r9/vm_<tag>_{fs_p0_30,fs_p2_60,iv,iv5}.txt
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
b=$1; tag=$2; shift 2
env "$@" LM_HZ=30 LM_TURNPROF=0 ./sl/$b > r9/vm_${tag}_fs_p0_30.txt 2>/dev/null
env "$@" LM_HZ=60 LM_TURNPROF=2 ./sl/$b > r9/vm_${tag}_fs_p2_60.txt 2>/dev/null
rm -f r9/vm_${tag}_iv.txt r9/vm_${tag}_iv5.txt
for sc in switchback lstair winder spiral curved; do
  env "$@" LM_HZ=60 LM_TURNPROF=2 ./sl/$b $sc >> r9/vm_${tag}_iv.txt 2>/dev/null
  env "$@" LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 ./sl/$b $sc >> r9/vm_${tag}_iv5.txt 2>/dev/null
done
