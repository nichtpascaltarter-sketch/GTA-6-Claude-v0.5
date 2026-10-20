#!/bin/sh
# surv.sh <binary> <tag> [ENV=1 ...]: interior scenarios with LM_IEV episodes, profiles 0/1/2 x 30/60 Hz x pace (own, 0.5)
# -> r9/sv_<tag>_p<prof>_<hz>_<pace>.{txt,err}
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
b=$1; tag=$2; shift 2
for pr in 0 1 2; do for hz in 30 60; do for pc in own 0.5; do
  o=r9/sv_${tag}_p${pr}_${hz}_${pc}
  rm -f $o.txt $o.err
  for sc in switchback lstair winder spiral curved; do
    if [ $pc = own ]; then env "$@" LM_IEV=1 LM_HZ=$hz LM_TURNPROF=$pr nice -n 4 ./sl/$b $sc >> $o.txt 2>> $o.err
    else env "$@" LM_IEV=1 LM_IV=$pc LM_HZ=$hz LM_TURNPROF=$pr nice -n 4 ./sl/$b $sc >> $o.txt 2>> $o.err; fi
  done
done; done; done
