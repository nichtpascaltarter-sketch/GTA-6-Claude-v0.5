#!/bin/sh
# facecam_args.sh TAG EXE ARGSFILE [extra args] : the character viewer with the shots of ARGSFILE (--shot ... --facecam
# ...), one exe, into /tmp/fx/fc/TAG_*.png (same setup as facecam_run.sh: street lamp, quality 1)
TAG=$1; EXE=$2; AF=$3; shift 3; EXTRA="$*"
ARGS=$(cat $AF)
mkdir -p /tmp/fx/fc/$TAG
cd /home/user/GTA-6-Claude-v0.5
WINEPREFIX=/tmp/wine_fx EXE=$EXE TIMEOUT=${TIMEOUT:-3000} nice -n 10 tools/run.sh --width ${W:-960} --height ${H:-540} --viewer characters --clip 0 --settle ${SETTLE:-20} --quality 1 --viewerlamp $ARGS $EXTRA --shotdir "Z:\\tmp\\fx\\fc\\$TAG\\" > /tmp/fx/fc/run_$TAG.log 2>&1
echo "exit $?" >> /tmp/fx/fc/run_$TAG.log
cp /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/fx/fc/log_$TAG.txt
for f in /tmp/fx/fc/$TAG/*.bmp; do [ -f "$f" ] && convert "$f" "/tmp/fx/fc/${TAG}_$(basename $f .bmp).png" && rm -f "$f"; done
echo done > /tmp/fx/fc/$TAG.done
