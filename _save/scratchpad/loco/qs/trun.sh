#!/bin/sh
# usage: trun.sh <tag> <env...>: the turnwalk / turnspot scenarios at 30 and 60 Hz -> tw_<tag>_<hz>.txt
tag=$1; shift
A2="LM_QSHOLD=0.85 LM_QSSNAP=1 LM_DZMOVE=1 LM_STEPFIT=1 LM_SEENWALK=1 LM_STANDSKIP=1 LM_STANDPLANTSTEP=1 LM_DZCLAMP=2"
for hz in 30 60; do
  ( env $A2 "$@" LM_HZ=$hz ./lm_qs turnwalk > tw_${tag}_$hz.txt 2>/dev/null; env $A2 "$@" LM_HZ=$hz ./lm_qs turnspot >> tw_${tag}_$hz.txt 2>/dev/null ) &
done
wait
