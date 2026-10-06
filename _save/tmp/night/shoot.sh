#!/bin/sh
# usage: SHOTS="x,y,z,yaw,pitch,hour,name ..." EXE=bin/x.exe [W= H= Q=] sh shoot.sh <prefix> [extra args...]
# PNGs: /tmp/night/shots/<prefix>_<name>.png; log /tmp/night/shots/log_<prefix>.txt; run output run_<prefix>.log
cd /home/user/GTA-6-Claude-v0.5
P=$1; shift
mkdir -p /tmp/night/shots/$P
ARGS=""
for s in $SHOTS; do ARGS="$ARGS --shot $s"; done
sh /tmp/night/gate.sh 3000
NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=${EXE:-bin/nt_snap.exe} TIMEOUT=${TIMEOUT:-5400} nice -n 10 tools/run.sh --width ${W:-1920} --height ${H:-1080} --quality ${Q:-3} --settle ${SETTLE:-16} $ARGS --shotdir "Z:\\tmp\\night\\shots\\$P\\" "$@" > /tmp/night/shots/run_$P.log 2>&1
rc=$?
echo "exit $rc" >> /tmp/night/shots/run_$P.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/shots/log_$P.txt
for s in $SHOTS; do n=$(echo $s | awk -F, '{print $7}'); if [ -f /tmp/night/shots/$P/$n.bmp ]; then convert /tmp/night/shots/$P/$n.bmp /tmp/night/shots/${P}_$n.png && rm /tmp/night/shots/$P/$n.bmp; fi; done
echo "rc $rc" > /tmp/night/shots/$P.done
