#!/bin/sh
# usage: SHOTS="x,y,z,yaw,pitch,hour,name ..." shoot.sh <prefix> [extra args...]
cd /home/user/GTA-6-Claude-v0.5
P=$1; shift
mkdir -p /tmp/fx/$P
ARGS=""
for s in $SHOTS; do ARGS="$ARGS --shot $s"; done
WINEPREFIX=${PREFIX:-/tmp/wine_fx} EXE=${EXE:-bin/nt_fx.exe} TIMEOUT=${TIMEOUT:-900} nice -n 10 tools/run.sh --width ${W:-960} --height ${H:-540} $ARGS --shotdir "Z:\\tmp\\fx\\$P\\" "$@" > /tmp/fx/run_$P.log 2>&1
echo "exit $?"
cp ${PREFIX:-/tmp/wine_fx}/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/log_$P.txt
for s in $SHOTS; do n=$(echo $s | awk -F, '{print $7}'); if [ -f /tmp/fx/$P/$n.bmp ]; then convert /tmp/fx/$P/$n.bmp /tmp/fx/${P}_$n.png && rm /tmp/fx/$P/$n.bmp; fi; done
ls /tmp/fx/${P}_* 2>/dev/null
