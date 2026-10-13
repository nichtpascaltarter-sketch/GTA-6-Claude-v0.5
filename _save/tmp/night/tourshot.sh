#!/bin/sh
# tourshot.sh TAG EXE START COUNT : autoplay tour shots at W x H (default 1920x1080) quality Q (default 3), no timers
T=$1; EXE=$2; START=$3; COUNT=$4; shift 4
mkdir -p /tmp/night/tour/$T
cd /home/user/GTA-6-Claude-v0.5
sh /tmp/night/gate.sh ${GATE:-3000}
NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=$EXE TIMEOUT=${TIMEOUT:-5400} nice -n 10 tools/run.sh --width ${W:-1920} --height ${H:-1080} --play --autoplay tour --tourstart $START --tourcount $COUNT --renderevery ${RE:-3} --quality ${Q:-3} "$@" --shotdir "Z:\\tmp\\night\\tour\\$T\\" > /tmp/night/tour/run_$T.log 2>&1
echo "exit $?" >> /tmp/night/tour/run_$T.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/tour/log_$T.txt
for f in /tmp/night/tour/$T/*.bmp; do [ -f "$f" ] && convert "$f" "/tmp/night/tour/${T}_$(basename $f .bmp).png" && rm -f "$f"; done
rmdir /tmp/night/tour/$T 2>/dev/null
echo done > /tmp/night/tour/$T.done
