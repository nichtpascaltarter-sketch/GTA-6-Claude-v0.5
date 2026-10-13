#!/bin/sh
# eye-region grid: rows = people x shots, columns = dirs: grid_eyes.sh <out> <people> <shots> dir:label ...
OUT=$1; PEOPLE=$2; SHOTS=$3; shift 3
T=/tmp/faces/mtmp_e; rm -rf $T; mkdir -p $T
k=0
for c in $(echo $PEOPLE | tr ',' ' '); do
  for s in $(echo $SHOTS | tr ',' ' '); do
    case $s in
      0p5m*) G="240x110+360+190"; R="200%";;
      1m*)   G="120x55+420+225"; R="400%";;
      *)     G="100x56+430+224"; R="480%";;
    esac
    j=0
    for dl in "$@"; do
      D=${dl%%:*}; L=${dl#*:}
      f=$D/c${c}_$s.png
      if [ -f $f ]; then
        convert $f -crop $G +repage -filter Lanczos -resize $R -gravity NorthWest -fill white -undercolor '#00000080' -pointsize 13 -annotate +3+3 "$L c$c $s" $T/r$(printf %03d $k)_$j.png
      else
        convert -size 480x220 xc:gray20 $T/r$(printf %03d $k)_$j.png
      fi
      j=$((j+1))
    done
    convert $T/r$(printf %03d $k)_*.png +append $T/row_$(printf %03d $k).png
    k=$((k+1))
  done
done
convert $T/row_*.png -append $OUT
echo $OUT
