#!/bin/sh
# usage: shots.sh "idx idx ..." [ground] [hour] [extra args]
cd /home/user/GTA-6-Claude-v0.5
G=${2:-6.0}
H=${3:-11}
ARGS=""
for i in $1; do
  X=$(python3 -c "print(-300+8*$i+4.2)")
  Z=$(python3 -c "print($G+2.0)")
  ARGS="$ARGS --shot $X,1505.6,$Z,145,-13,$H,vm_$i"
done
export WINEPREFIX=/tmp/neontide_wine_vm
EXE=bin/nt_vehicles.exe TIMEOUT=900 tools/run.sh --viewer vehicles --width 960 --height 540 --settle 10 $ARGS $4 > /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/wine.log 2>&1
for i in $1; do convert /tmp/vm_$i.bmp /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/out/e$i.png 2>/dev/null; done
