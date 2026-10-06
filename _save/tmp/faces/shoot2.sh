#!/bin/sh
# usage: EXE=... shoot2.sh <prefix> <args...>   (args include --shot/--facecam pairs)
# writes /tmp/faces/<prefix>/<name>.png and /tmp/faces/log_<prefix>.txt
cd /home/user/GTA-6-Claude-v0.5
P=$1; shift
mkdir -p /tmp/faces/$P
PFX=${PREFIX:-/tmp/wine_faces}
WINEPREFIX=$PFX EXE=${EXE:-/tmp/faces/nt.exe} TIMEOUT=${TIMEOUT:-1800} nice -n 6 tools/run.sh --width ${W:-960} --height ${H:-540} "$@" --shotdir "Z:\\tmp\\faces\\$P\\" > /tmp/faces/run_$P.log 2>&1
echo "exit $?"
cp $PFX/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/faces/log_$P.txt 2>/dev/null
for f in /tmp/faces/$P/*.bmp; do [ -f "$f" ] && convert "$f" "${f%.bmp}.png" && rm "$f"; done
ls /tmp/faces/$P/ 2>/dev/null | head -60
grep -i -E "fatal|vkd3d|error" /tmp/faces/log_$P.txt | head -5
