#!/bin/sh
# usage: vshots.sh "idx idx ..." prefix [extra args]   (vehicle viewer; ground ~6.0 at the line-up)
cd /home/user/GTA-6-Claude-v0.5
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/car/wine
G=6.0
ARGS=""
for i in $1; do
  X=$(python3 -c "print(-300+8*$i+4.2)")
  XL=$(python3 -c "print(-300+8*$i-4.2)")
  Z=$(python3 -c "print($G+1.9)")
  ARGS="$ARGS --shot $X,1505.6,$Z,145,-13,11,${2}_r$i --shot $XL,1505.6,$Z,-145,-13,11,${2}_l$i"
done
export WINEPREFIX=/tmp/wine_anim
EXE=bin/anim_doors.exe TIMEOUT=900 nice -n 6 tools/run.sh --viewer vehicles --width 960 --height 540 --settle 10 $ARGS $3 > $S/${2}.log 2>&1
echo "exit $?"
for i in $1; do for sd in r l; do convert /tmp/${2}_$sd$i.bmp $S/${2}_$sd$i.png 2>/dev/null && rm -f /tmp/${2}_$sd$i.bmp; done; done
