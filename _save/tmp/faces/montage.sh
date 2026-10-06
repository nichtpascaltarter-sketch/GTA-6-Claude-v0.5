#!/bin/sh
# usage: montage.sh <outfile> <dirA> <dirB> <labelA> <labelB> <char> [shots...]
# shots: names like 0p5m_day 1m_day 4m_day 1m_night 4m_night ; crops centred on the eyes (facecam: image centre)
OUT=$1; A=$2; B=$3; LA=$4; LB=$5; C=$6; shift 6
T=/tmp/faces/mtmp; rm -rf $T; mkdir -p $T
n=0
for s in "$@"; do
  case $s in
    0p5m*) G="320x260+320+150"; R="150%";;
    1m*)   G="170x140+395+205"; R="282%";;
    4m*)   G="96x80+432+232";   R="500%";;
    *)     G="320x260+320+150"; R="150%";;
  esac
  for side in A B; do
    if [ $side = A ]; then D=$A; L=$LA; else D=$B; L=$LB; fi
    f=$D/c${C}_$s.png
    if [ -f $f ]; then
      convert $f -crop $G +repage -filter Lanczos -resize $R -gravity NorthWest -fill white -undercolor '#00000080' -pointsize 15 -annotate +4+4 "$L  c$C $s" $T/$(printf %02d $n)_$side.png
    else
      convert -size 480x390 xc:gray20 -fill white -pointsize 16 -annotate +10+30 "missing $f" $T/$(printf %02d $n)_$side.png
    fi
  done
  n=$((n+1))
done
montage $T/*.png -tile 2x -geometry +2+2 -background black $OUT
echo $OUT
