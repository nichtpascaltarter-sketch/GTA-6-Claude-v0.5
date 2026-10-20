#!/bin/sh
# usage: qrun2.sh <bin> <tag> <env...>: crowd, blocked, blocked+SEPVEL at 30 and 60 Hz, 8 seeds; metrics q_<tag>_<hz>_<k>[b|s].txt,
# quick-step logs qq_<tag>_<hz>_<k>[b|s].txt
bin=$1; tag=$2; shift 2
for hz in 30 60; do
  for k in 0 1 2 3 4 5 6 7; do
    env "$@" LM_HZ=$hz LM_SEEDOFF=$k LM_QSLOG=1 $bin stairs17.c > q_${tag}_${hz}_$k.txt 2> qq_${tag}_${hz}_$k.txt &
    env "$@" LM_HZ=$hz LM_SEEDOFF=$k LM_QSLOG=1 $bin stairs17.b > q_${tag}_${hz}_${k}b.txt 2> qq_${tag}_${hz}_${k}b.txt &
    env "$@" LM_HZ=$hz LM_SEEDOFF=$k LM_QSLOG=1 LM_SEPVEL=1 $bin stairs17.b > q_${tag}_${hz}_${k}s.txt 2> qq_${tag}_${hz}_${k}s.txt &
    wait
  done
done
