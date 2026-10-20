#!/bin/sh
# preview of the buzz-cut people (viewer 6, 27, 35, 67), cards off (FACES_BUZZOFF) / on: <previewBin> <tag> [view] [dist] [extra env]
B=$1; T=$2; V=${3:-front}; D=${4:-0.45}
O=/tmp/faces/pvb/$T; mkdir -p $O
for c in "6 48514 6" "27 214813 6" "35 278165 0" "67 531573 4"; do
  set -- $c
  for m in off on; do
    if [ $m = off ]; then E="FACES_BUZZOFF=1"; else E="PV_ON=1"; fi
    env $E PREVIEW_TGT=${TGT:-0,0.01,0.075} PREVIEW_LIGHT=${LIGHT:-0.3,0.6,0.75} $B $O/c$1_$m.ppm --seed $2 --role $3 --view $V --tiles --dist $D --w ${PW:-360} --h ${PW:-360} --ss 2 > /dev/null 2>&1
    convert $O/c$1_$m.ppm $O/c$1_$m.png && rm $O/c$1_$m.ppm
  done
done
cd $O && convert \( c6_off.png c27_off.png c35_off.png c67_off.png +append \) \( c6_on.png c27_on.png c35_on.png c67_on.png +append \) -append m.png
echo $O/m.png
