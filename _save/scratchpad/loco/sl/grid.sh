#!/bin/sh
# usage: grid.sh <tag> <env...>: stairs at 60 and 30 Hz (and blocked with LM_SEPVEL), outputs g_<tag>_<hz>[s].txt
tag=$1; shift
for hz in 60 30; do
  env "$@" LM_HZ=$hz ./lm_sl stairs > g_${tag}_$hz.txt &
  env "$@" LM_HZ=$hz LM_SEPVEL=1 ./lm_sl stairs17.blocked > g_${tag}_${hz}s.txt &
done
wait
