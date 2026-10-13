#!/bin/sh
# facecam_run.sh TAG EXE people dists hours [yawDay] [extra args] : facecam close-ups with one exe into /tmp/fx/fc/TAG_*.png
TAG=$1; EXE=$2; PEOPLE=$3; DISTS=$4; HOURS=$5; YAW=${6:--45}; shift 6 2>/dev/null; EXTRA="$*"
ARGS=$(python3 /tmp/fx/facecam_shots.py $PEOPLE $DISTS $HOURS $YAW)
mkdir -p /tmp/fx/fc/$TAG
cd /home/user/GTA-6-Claude-v0.5
WINEPREFIX=/tmp/wine_fx EXE=$EXE TIMEOUT=${TIMEOUT:-3000} nice -n 10 tools/run.sh --width ${W:-960} --height ${H:-540} --viewer characters --clip 0 --settle ${SETTLE:-20} --quality 1 --viewerlamp $ARGS $EXTRA --shotdir "Z:\\tmp\\fx\\fc\\$TAG\\" > /tmp/fx/fc/run_$TAG.log 2>&1
echo "exit $?" >> /tmp/fx/fc/run_$TAG.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/fc/log_$TAG.txt
for f in /tmp/fx/fc/$TAG/*.bmp; do [ -f "$f" ] && convert "$f" "/tmp/fx/fc/${TAG}_$(basename $f .bmp).png" && rm -f "$f"; done
echo done > /tmp/fx/fc/$TAG.done
