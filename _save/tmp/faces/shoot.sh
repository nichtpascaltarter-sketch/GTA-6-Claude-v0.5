#!/bin/sh
# usage: SHOTS="x,y,z,yaw,pitch,hour,name ..." EXE=... shoot.sh <prefix> [extra args...]
# writes /tmp/faces/<prefix>/<name>.png and /tmp/faces/log_<prefix>.txt
cd /home/user/GTA-6-Claude-v0.5
P=$1; shift
mkdir -p /tmp/faces/$P
ARGS=""
for s in $SHOTS; do ARGS="$ARGS --shot $s"; done
PFX=${PREFIX:-/tmp/wine_faces}
WINEPREFIX=$PFX EXE=${EXE:-/tmp/faces/nt.exe} TIMEOUT=${TIMEOUT:-900} nice -n 6 tools/run.sh --width ${W:-960} --height ${H:-540} $ARGS --shotdir "Z:\\tmp\\faces\\$P\\" "$@" > /tmp/faces/run_$P.log 2>&1
echo "exit $?"
cp $PFX/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/faces/log_$P.txt 2>/dev/null
for s in $SHOTS; do n=$(echo $s | awk -F, '{print $7}'); if [ -f /tmp/faces/$P/$n.bmp ]; then convert /tmp/faces/$P/$n.bmp /tmp/faces/$P/$n.png && rm /tmp/faces/$P/$n.bmp; fi; done
ls /tmp/faces/$P/ 2>/dev/null | head -50
grep -i -E "fatal|vkd3d|error" /tmp/faces/log_$P.txt | head -5
