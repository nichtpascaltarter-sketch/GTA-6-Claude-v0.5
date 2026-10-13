#!/bin/sh
# usage: frames.sh out.png "t1 t2 ..." "carview args (incl. --enter/--exit placeholder T)"  -> labelled montage
# e.g. frames.sh in.png "0.3 0.5" "--model 3 --seat 0 --char 1031 --enter T --view rear34"
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car
OUT=$1; TS=$2; ARGS=$3
LIST=""
for t in $TS; do
  A=$(echo "$ARGS" | sed "s/ T/ $t/")
  ${CV:-$S/carview} $S/f_$t.ppm $A --w 480 --h 360 --ss 2 > $S/f_$t.txt 2>&1
  convert $S/f_$t.ppm -gravity north -fill white -pointsize 18 -annotate +0+4 "t $t $(grep -o '[0-9]* vertices' $S/f_$t.txt)" $S/f_$t.png
  rm -f $S/f_$t.ppm
  LIST="$LIST $S/f_$t.png"
done
montage $LIST -tile 4x -geometry +2+2 -background gray20 $OUT
rm -f $LIST $S/f_*.txt
