#!/bin/sh
# before/after grid: rows = people x shots, columns = before / after: grid_ab.sh <out> <dirA:labelA> <dirB:labelB> <prefix> <people> <shots> [crop=hair|face|hat]
OUT=$1; A=$2; B=$3; P=$4; PEOPLE=$5; SHOTS=$6; CROP=${7:-hair}
T=/tmp/faces/mtmp_ab; rm -rf $T; mkdir -p $T
k=0
for c in $(echo $PEOPLE | tr ',' ' '); do
  for s in $(echo $SHOTS | tr ',' ' '); do
    case "$CROP:$s" in
      hair:0p5m*) G="200x90+380+160"; R="240%";;
      hair:1m*)   G="100x45+430+206"; R="480%";;
      hair:4m*)   G="100x56+430+224"; R="480%";;
      hat:0p5m*)  G="240x150+360+170"; R="200%";;
      hat:1m*)    G="120x75+420+205"; R="400%";;
      *)          G="320x260+320+150"; R="150%";;
    esac
    for side in A B; do
      if [ $side = A ]; then dl=$A; else dl=$B; fi
      D=${dl%%:*}; L=${dl#*:}
      f=$D/$P${c}_$s.png
      if [ -f $f ]; then
        convert $f -crop $G +repage -filter Lanczos -resize $R -gravity NorthWest -fill white -undercolor '#00000080' -pointsize 14 -annotate +4+4 "$L  $P$c $s" $T/$(printf %03d $k)_$side.png
      else
        convert -size 480x216 xc:gray20 -fill white -pointsize 14 -annotate +10+30 "missing $f" $T/$(printf %03d $k)_$side.png
      fi
    done
    k=$((k+1))
  done
done
montage $T/*.png -tile 2x -geometry +2+2 -background black $OUT
echo $OUT
