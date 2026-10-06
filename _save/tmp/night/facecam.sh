#!/bin/sh
# facecam.sh TAG EXE ARGSFILE [extra args] : character viewer portraits (street lamp, quality 1, like /tmp/fx/facecam_args.sh)
# into /tmp/night/fc/TAG_*.png; marker /tmp/night/fc/TAG.done
TAG=$1; EXE=$2; AF=$3; shift 3; EXTRA="$*"
ARGS=$(cat $AF)
mkdir -p /tmp/night/fc/$TAG
cd /home/user/GTA-6-Claude-v0.5
sh /tmp/night/gate.sh 3000
NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=$EXE TIMEOUT=${TIMEOUT:-3000} nice -n 10 tools/run.sh --width ${W:-960} --height ${H:-540} --viewer characters --clip 0 --settle ${SETTLE:-20} --quality 1 --viewerlamp $ARGS $EXTRA --shotdir "Z:\\tmp\\night\\fc\\$TAG\\" > /tmp/night/fc/run_$TAG.log 2>&1
echo "exit $?" >> /tmp/night/fc/run_$TAG.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/night/fc/log_$TAG.txt
for f in /tmp/night/fc/$TAG/*.bmp; do [ -f "$f" ] && convert "$f" "/tmp/night/fc/${TAG}_$(basename $f .bmp).png" && rm -f "$f"; done
echo done > /tmp/night/fc/$TAG.done
