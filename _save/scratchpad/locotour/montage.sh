#!/bin/sh
# usage: montage.sh before|after   - BMP shots to PNG, then one montage per burst (4 columns, the shot number on each tile)
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
t=$1
D=$T/shots_$t
cd $D || exit 1
for f in *.bmp; do [ -e "$f" ] || continue; convert "$f" "${f%.bmp}.png" && rm -f "$f"; done
mkdir -p $T/montage
for b in $(ls loco_b*_??.png 2>/dev/null | sed 's/^loco_b\([0-9]*\)_.*/\1/' | sort -u); do
  kind=$(ls loco_b${b}_*_00.png | sed 's/^loco_b[0-9]*_\([a-z]*\)_00.png/\1/')
  montage $(ls loco_b${b}_${kind}_*.png) -tile 4x -geometry 480x270+2+2 -background '#202020' -pointsize 18 -fill white \
    -title "$t: burst $b ($kind), 0.1 s apart" $T/montage/${t}_b${b}_${kind}.png
done
ls $T/montage | grep "^${t}_"
