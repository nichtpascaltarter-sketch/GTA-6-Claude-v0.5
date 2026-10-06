#!/bin/sh
# render.sh TAG RIMSPEC : top-lit front, front-lit front, top-lit 3/4 for c38, c0, c17 -> TAG_<c>_<view>.ppm
TAG=$1; RIM=$2
for c in "c38 301922 3" "c0 1000 0" "c17 135623 3" "c7 56433 0"; do
  set -- $c
  for v in "top 0,0.45,0.9 face" "front 0.3,1,0.35 face" "top3 0,0.45,0.9 face3"; do
    set -- $c $v
    FACES_RIM=$RIM PREVIEW_NOHAT=1 PREVIEW_SHADE=diffuse PREVIEW_LIGHT=$5 /tmp/faces/preview_d11 ${TAG}_$1_$4.ppm --seed $2 --role $3 --view $6 --dist 0.3 --w 300 --h 300 --ss 2 > /dev/null 2>&1
  done
done
