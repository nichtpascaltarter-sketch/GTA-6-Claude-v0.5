#!/bin/sh
# usage: SHOTS="x,y,z,yaw,pitch,hour,name ..." shoot.sh <prefix> [extra args...]   -> /tmp/charshots/<prefix>_<name>.png
cd /home/user/GTA-6-Claude-v0.5
P=$1; shift
mkdir -p /tmp/charshots/$P
ARGS=""
for s in $SHOTS; do ARGS="$ARGS --shot $s"; done
WINEPREFIX=/tmp/wine_char EXE=${EXE:-bin/nt_char.exe} TIMEOUT=${TIMEOUT:-1500} nice -n 10 tools/run.sh --width ${W:-960} --height ${H:-540} $ARGS --shotdir "Z:\\tmp\\charshots\\$P\\" "$@" > /tmp/charshots/run_$P.log 2>&1
echo "exit $?"
cp /tmp/wine_char/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/charshots/log_$P.txt 2>/dev/null
for s in $SHOTS; do n=$(echo $s | awk -F, '{print $7}'); if [ -f /tmp/charshots/$P/$n.bmp ]; then convert /tmp/charshots/$P/$n.bmp /tmp/charshots/${P}_$n.png && rm /tmp/charshots/$P/$n.bmp; fi; done
ls /tmp/charshots/${P}_* 2>/dev/null
