#!/bin/sh
# mouth grid: rows = people x shots, columns = dirs: grid_mouth.sh <out> <people> <shots> dir:label ...
OUT=$1; PEOPLE=$2; SHOTS=$3; shift 3
T=/tmp/faces/mtmp_m; rm -rf $T; mkdir -p $T
k=0
for c in $(echo $PEOPLE | tr ',' ' '); do
  for s in $(echo $SHOTS | tr ',' ' '); do
    case $s in
      0p5m*) G="160x80+400+300"; R="300%";;
      1m*)   G="80x40+440+284"; R="600%";;
      *)     G="60x40+450+275"; R="800%";;
    esac
    j=0
    for dl in "$@"; do
      D=${dl%%:*}; L=${dl#*:}
      f=$D/c${c}_$s.png; [ -f $f ] || f=$D/c${c}_$s.bmp
      if [ -f $f ]; then
        convert $f -crop $G +repage -filter Lanczos -resize $R -gravity NorthWest -fill white -undercolor '#00000080' -pointsize 13 -annotate +3+3 "$L c$c $s" $T/r$(printf %03d $k)_$j.png
      else
        convert -size 480x240 xc:gray20 $T/r$(printf %03d $k)_$j.png
      fi
      j=$((j+1))
    done
    convert $T/r$(printf %03d $k)_*.png +append $T/row_$(printf %03d $k).png
    k=$((k+1))
  done
done
convert $T/row_*.png -append $OUT
echo $OUT
