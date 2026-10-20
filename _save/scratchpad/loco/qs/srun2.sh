#!/bin/sh
# usage: srun2.sh <bin> <tag> <env...>: stairs17.stops, 30 and 60 Hz, 4 seeds -> ss_<tag>_<hz>_<k>.txt
bin=$1; tag=$2; shift 2
for hz in 30 60; do
  for k in 0 1 2 3; do env "$@" LM_HZ=$hz LM_SEEDOFF=$k $bin stairs17.stops > ss_${tag}_${hz}_$k.txt 2>/dev/null & done
  wait
done
