#!/bin/sh
# finish a showcase set that a container restart cut short: render the lost shots, convert, mark done
# usage: fill_set.sh OUTDIR "--shot ... [--shot ...]"
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
OUT=${1:?}; SHOTS=${2:?}
sh $SP/fillshowcase.sh $OUT "$SHOTS"
rc=$?
for f in $OUT/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm -f "$f"; done
echo "SHOWCASE DONE rc=$rc $OUT $(ls $OUT/*.png 2>/dev/null | wc -l) png (finished by fill_set.sh after a restart)"
