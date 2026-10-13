#!/bin/sh
# usage: topframes.sh out.png "t1 t2 ..." "carview args (T = time)" [cutz] [target] [dist] -> cut-away top-view montage
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car
OUT=$1; TS=$2; ARGS=$3; CZ=${4:-1.0}; TG=${5:--0.5,-0.8,0.5}; DS=${6:-3.2}
LIST=""
for t in $TS; do
  A=$(echo "$ARGS" | sed "s/ T/ $t/")
  CARVIEW_CUTZ=$CZ ${CV:-$S/carview} $S/g_$t.ppm $A --view top --pitch 89 --target $TG --dist $DS --w 400 --h 400 --ss 2 > /dev/null 2>&1
  convert $S/g_$t.ppm -gravity north -fill black -pointsize 18 -annotate +0+4 "t $t" $S/g_$t.png
  rm -f $S/g_$t.ppm
  LIST="$LIST $S/g_$t.png"
done
montage $LIST -tile 4x -geometry +2+2 -background gray20 $OUT
rm -f $LIST
