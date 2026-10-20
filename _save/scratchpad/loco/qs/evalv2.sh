#!/bin/sh
# evalv2.sh (interior x4: LM_IREP=4) <binary> <tag> [ENV=1 ...]: one variant's picture -> r9/ev_<tag>_*: full suites p0_30 / p2_60 (fs_*), the
# interior survey with episodes (sv_*: profiles 0/1/2 x 30/60 Hz x own pace / 0.5), the straight stairs at 0.5 (s5_*)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
b=$1; tag=$2; shift 2
o=r9/ev2_$tag
env "$@" LM_HZ=30 LM_TURNPROF=0 nice -n 4 ./sl/$b > ${o}_fs_p0_30.txt 2>/dev/null
env "$@" LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/$b > ${o}_fs_p2_60.txt 2>/dev/null
for pr in 0 1 2; do for hz in 30 60; do
  f=${o}_sv_p${pr}_${hz}
  rm -f ${f}_own.txt ${f}_own.err ${f}_0.5.txt ${f}_0.5.err
  for sc in switchback lstair winder spiral curved; do
    env "$@" LM_IREP=4 LM_IEV=1 LM_HZ=$hz LM_TURNPROF=$pr nice -n 4 ./sl/$b $sc >> ${f}_own.txt 2>> ${f}_own.err
    env "$@" LM_IREP=4 LM_IEV=1 LM_IV=0.5 LM_HZ=$hz LM_TURNPROF=$pr nice -n 4 ./sl/$b $sc >> ${f}_0.5.txt 2>> ${f}_0.5.err
  done
done; done
env "$@" LM_SV=0.5 LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/$b stairs > ${o}_s5_60.txt 2>/dev/null
env "$@" LM_SV=0.5 LM_HZ=30 LM_TURNPROF=0 nice -n 4 ./sl/$b stairs > ${o}_s5_30.txt 2>/dev/null
cat ${o}_sv_*_own.txt > ${o}_ivall_own.txt; cat ${o}_sv_*_0.5.txt > ${o}_ivall_0.5.txt
