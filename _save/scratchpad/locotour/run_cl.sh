#!/bin/sh
# usage: run_cl.sh <tree> <tag>  - one --autoplay couple run (close-up test camera) of a clasp tree's exe at 1280x720;
# the shots converted to PNG (BMPs deleted), the log to log_<tag>.txt
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
t=$1; tag=$2
cd $T/$t || exit 1
mkdir -p $T/shots_$tag
export WINEPREFIX=/tmp/wine_anim
LOG=/tmp/wine_anim/drive_c/users/root/AppData/Local/NeonTide/log.txt
D="Z:$(echo $T/shots_$tag/ | sed 's|/|\\|g')"
EXE=bin/$t.exe TIMEOUT=1800 nice -n 6 sh tools/run.sh --width 1280 --height 720 --play --autoplay couple --renderevery 6 --quality 2 --shotdir "$D" > $T/run_$tag.txt 2>&1
echo "run $tag exit $?"
cp $LOG $T/log_$tag.txt
for f in $T/shots_$tag/*.bmp; do [ -f "$f" ] && python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$f" "${f%.bmp}.png" && rm -f "$f"; done
ls $T/shots_$tag | wc -l
