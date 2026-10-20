#!/bin/sh
# grid of hair crops: one column per person, one row per shot: grid_hair.sh <out> <dir> <prefix> <people> [shots]
OUT=$1; D=$2; P=$3; PEOPLE=$4; shift 4
[ $# -eq 0 ] && set -- 0p5m_day 1m_day 4m_day 0p5m_night 1m_night 4m_night
T=/tmp/faces/mtmp_g; rm -rf $T; mkdir -p $T
for c in $(echo $PEOPLE | tr ',' ' '); do
  k=0
  for s in "$@"; do
    case $s in
      0p5m*) G="200x90+380+160"; R="240%";;
      1m*)   G="100x45+430+206"; R="480%";;
      4m*)   G="100x56+430+224"; R="480%";;
    esac
    f=$D/$P${c}_$s.png
    convert $f -crop $G +repage -filter Lanczos -resize $R -gravity NorthWest -fill white -undercolor '#00000080' -pointsize 13 -annotate +3+3 "$c $s" $T/c${c}_$(printf %02d $k).png
    k=$((k+1))
  done
  convert $T/c${c}_*.png -append $T/col_$c.png
done
convert $(for c in $(echo $PEOPLE | tr ',' ' '); do echo $T/col_$c.png; done) +append $OUT
echo $OUT
