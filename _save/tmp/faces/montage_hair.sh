#!/bin/sh
# hair crops (top of the head) for A/B: montage_hair.sh <outfile> <dirA> <dirB> <labelA> <labelB> <prefix> <person> [shots...]
OUT=$1; A=$2; B=$3; LA=$4; LB=$5; P=$6; C=$7; shift 7
T=/tmp/faces/mtmp_h; rm -rf $T; mkdir -p $T
[ $# -eq 0 ] && set -- 0p5m_day 1m_day 4m_day 0p5m_night 1m_night 4m_night
n=0
for s in "$@"; do
  case $s in
    0p5m*) G="200x90+380+160"; R="240%";;
    1m*)   G="100x45+430+206"; R="480%";;
    4m*)   G="100x56+430+224"; R="480%";;
  esac
  for side in A B; do
    if [ $side = A ]; then D=$A; L=$LA; else D=$B; L=$LB; fi
    f=$D/$P${C}_$s.png; [ -f $f ] || f=$D/$P${C}_$s.bmp
    if [ -f $f ]; then
      convert $f -crop $G +repage -filter Lanczos -resize $R -gravity NorthWest -fill white -undercolor '#00000080' -pointsize 14 -annotate +4+4 "$L  $C $s" $T/$(printf %02d $n)_$side.png
    else
      convert -size 480x216 xc:gray20 -fill white -pointsize 14 -annotate +10+30 "missing $f" $T/$(printf %02d $n)_$side.png
    fi
  done
  n=$((n+1))
done
montage $T/*.png -tile 2x -geometry +2+2 -background black $OUT
echo $OUT
