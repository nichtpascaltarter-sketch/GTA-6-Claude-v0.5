#!/bin/sh
# ear grid: grid_ears.sh <out> <people> <shots> dir:label ...  (rows = people x shots, columns = dirs)
OUT=$1; PEOPLE=$2; SHOTS=$3; shift 3
T=/tmp/faces/mtmp_e; rm -rf $T; mkdir -p $T
k=0
for c in $(echo $PEOPLE | tr ',' ' '); do
  for s in $(echo $SHOTS | tr ',' ' '); do
    case $s in
      0p5m*) G="130x150+295+215"; R="200%";;
      1m*)   G="70x80+383+250"; R="372%";;
      *)     G="40x45+400+262"; R="650%";;
    esac
    j=0
    for dl in "$@"; do
      D=${dl%%:*}; L=${dl#*:}
      f=$D/c${c}_$s.png; [ -f $f ] || f=$D/c${c}_$s.bmp
      if [ -f $f ]; then
        convert $f -crop $G +repage -filter Lanczos -resize $R -gravity NorthWest -fill white -undercolor '#00000080' -pointsize 13 -annotate +3+3 "$L c$c $s" $T/r$(printf %03d $k)_$j.png
      else
        convert -size 260x300 xc:gray20 $T/r$(printf %03d $k)_$j.png
      fi
      j=$((j+1))
    done
    convert $T/r$(printf %03d $k)_*.png +append $T/row_$(printf %03d $k).png
    k=$((k+1))
  done
done
convert $T/row_*.png -append $OUT
echo $OUT
