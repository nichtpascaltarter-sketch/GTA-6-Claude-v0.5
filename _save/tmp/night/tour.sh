#!/bin/sh
# tour.sh TAG EXE START COUNT [extra]: autoplay tour with --synctimers (960x540 Q1 renderevery 6), shots + log in /tmp/night/tour
T=$1; EXE=$2; START=$3; COUNT=$4; shift 4
mkdir -p /tmp/night/tour/$T
cd /home/user/GTA-6-Claude-v0.5
sh /tmp/night/gate.sh 3000
NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=$EXE TIMEOUT=${TIMEOUT:-5400} nice -n 10 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart $START --tourcount $COUNT --renderevery 6 --quality 1 --synctimers --gfxstats "$@" --shotdir "Z:\\tmp\\night\\tour\\$T\\" > /tmp/night/tour/run_$T.log 2>&1
echo "exit $?" >> /tmp/night/tour/run_$T.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/tour/log_$T.txt
for f in /tmp/night/tour/$T/*.bmp; do [ -f "$f" ] && convert "$f" "/tmp/night/tour/${T}_$(basename $f .bmp).png" && rm -f "$f"; done
echo done > /tmp/night/tour/$T.done
