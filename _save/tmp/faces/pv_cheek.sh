#!/bin/sh
# preview renders of the bearded men's cheeks under the viewer lamp's direction: <previewBin> <tag> [shade]
# c38 = seed 301922 role 3, c27 = seed 214813 role 6 (viewer: randomCharacter(1000 + i * 7919, i % 7))
B=$1; T=$2; SH=$3
O=/tmp/faces/pv/$T; mkdir -p $O
for m in 0 1 2; do
  for c in "38 301922 3" "27 214813 6"; do
    set -- $c
    FACES_AB=$m PREVIEW_LIGHT=0.15,0.39,0.91 PREVIEW_SHADE=$SH $B $O/c$1_ab$m.ppm --seed $2 --role $3 --view face --dist 0.42 --w 400 --h 400 --ss 2 > /dev/null 2>&1
    convert $O/c$1_ab$m.ppm $O/c$1_ab$m.png && rm $O/c$1_ab$m.ppm
  done
done
ls $O
