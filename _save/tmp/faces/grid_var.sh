#!/bin/sh
# variant grid for one person: columns = dirs (dir:label), rows = shots: grid_var.sh <out> <person> <shots,comma> dir:label ...
OUT=$1; C=$2; SHOTS=$3; shift 3
T=/tmp/faces/mtmp_v; rm -rf $T; mkdir -p $T
k=0
for dl in "$@"; do
  D=${dl%%:*}; L=${dl#*:}
  r=0
  for s in $(echo $SHOTS | tr ',' ' '); do
    case $s in
      0p5m*) G="200x90+380+160"; R="240%";;
      1m*)   G="100x45+430+206"; R="480%";;
      4m*)   G="100x56+430+224"; R="480%";;
    esac
    f=$D/z${C}_$s.png; [ -f $f ] || f=$D/z${C}_$s.bmp
    if [ -f $f ]; then
      convert $f -crop $G +repage -filter Lanczos -resize $R -gravity NorthWest -fill white -undercolor '#00000080' -pointsize 13 -annotate +3+3 "$L $C $s" $T/k$(printf %02d $k)_r$(printf %02d $r).png
    else
      convert -size 480x216 xc:gray20 $T/k$(printf %02d $k)_r$(printf %02d $r).png
    fi
    r=$((r+1))
  done
  convert $T/k$(printf %02d $k)_r*.png -append $T/col_$(printf %02d $k).png
  k=$((k+1))
done
convert $T/col_*.png +append $OUT
echo $OUT
