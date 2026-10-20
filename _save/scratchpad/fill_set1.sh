#!/bin/sh
# finish showcase set 1 after the 03:37 container restart: render the one lost shot, convert, mark done
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
OUT=/tmp/showcase/1020_0246
sh $SP/fillshowcase.sh $OUT "--shot 2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night"
rc=$?
for f in $OUT/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "$f"; done
echo "SHOWCASE DONE rc=$rc $OUT $(ls $OUT/*.png 2>/dev/null | wc -l) png (set 1 finished by fill_set1.sh after the restart)"
