#!/bin/sh
# usage: seeds.sh <tag> <hz> <env...>: crowd + blocked (+ blocked with LM_SEPVEL) for seed offsets 0..7 -> s_<tag>_<hz>_<k>[s].txt
tag=$1; hz=$2; shift 2
for k in 0 1 2 3 4 5 6 7; do
  env "$@" LM_HZ=$hz LM_SEEDOFF=$k ./lm_sl stairs17.c > s_${tag}_${hz}_$k.txt &
  env "$@" LM_HZ=$hz LM_SEEDOFF=$k ./lm_sl stairs17.b > s_${tag}_${hz}_${k}b.txt &
  env "$@" LM_HZ=$hz LM_SEEDOFF=$k LM_SEPVEL=1 ./lm_sl stairs17.b > s_${tag}_${hz}_${k}s.txt &
  wait
done
