#!/bin/sh
# proofsheet.sh OUT BEFORE AFTER VIEW... : rows "view: before | after" (each 960x540) into OUT (PNG)
OUT=$1; B=$2; A=$3; shift 3
T=/tmp/night/_ps; rm -rf $T; mkdir -p $T
i=0
for v in "$@"; do
  fb=/tmp/night/shots/${B}_$v.png; fa=/tmp/night/shots/${A}_$v.png
  [ -f "$fb" ] && [ -f "$fa" ] || continue
  convert "$fb" -resize 960x540 -gravity northwest -fill white -undercolor '#000000a0' -pointsize 22 -annotate +8+6 "$v  BEFORE ($B)" $T/b.png
  convert "$fa" -resize 960x540 -gravity northwest -fill white -undercolor '#000000a0' -pointsize 22 -annotate +8+6 "$v  AFTER ($A)" $T/a.png
  convert $T/b.png $T/a.png +append $T/row_$(printf %02d $i).png
  i=$((i+1))
done
convert $T/row_*.png -append "$OUT"
ls -la "$OUT"
