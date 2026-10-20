#!/bin/sh
# usage: srun.sh <tag> <env...>: stairs17.stops, 30 and 60 Hz, 4 seeds -> ss_<tag>_<hz>_<k>.txt
tag=$1; shift
for hz in 30 60; do
  for k in 0 1 2 3; do env "$@" LM_HZ=$hz LM_SEEDOFF=$k ./lm_qs stairs17.stops > ss_${tag}_${hz}_$k.txt 2>/dev/null & done
  wait
done
