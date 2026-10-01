#!/bin/sh
# usage: checks.sh "models" "seats" "heights" [dir]  -> one compact line per run
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car
for m in $1; do for st in $2; do
  if [ -n "$4" ]; then D="CARVIEW_DIR=$4"; else D="X=1"; fi
  env $D CARVIEW_HEIGHTS=$3 CARVIEW_SEAT=$st $S/carview /dev/null --model $m --check 2>&1 | grep " seat " | \
    sed -E 's/roof .* ground [0-9.]+\(b-?[0-9]+\)  //; s/\(b([0-9]+) at ([-0-9.]+) ([-0-9.]+) ([-0-9.]+)( [a-z ]+ mat [0-9]+)?\)/(b\1\5)/g; s/  \([0-9]+ ms\)//; s/, seated/ seated/'
done; done
