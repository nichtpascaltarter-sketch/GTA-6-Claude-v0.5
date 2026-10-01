#!/bin/sh
# before/after montages of the final proof set: <beforeDir> <afterDir> <tag>
B=$1; A=$2; T=$3
OUT=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/faces
mkdir -p $OUT
for f in $B/*.bmp $A/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
for c in 0 1 27 7 17 38; do
  case $c in 0) L="Mari";; 1) L="Dex";; 27) L="young dark-skinned bearded man";; 7) L="old woman";; 17) L="older light-skinned woman";; 38) L="full beard";; esac
  /tmp/faces/montage.sh $OUT/${T}_c${c}.png $B $A before after $c 0p5m_day 1m_day 4m_day 1m_night 4m_night > /dev/null
  echo "$OUT/${T}_c${c}.png ($L)"
done
