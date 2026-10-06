#!/bin/sh
# usage: perf.sh <exe> <tag> : club_night tour stop (201 peds), logs "cpu ms ... peds"
cd /home/user/GTA-6-Claude-v0.5
PFX=/tmp/wine_faces
mkdir -p /tmp/faces/perf_$2
WINEPREFIX=$PFX EXE=$1 TIMEOUT=1500 nice -n 6 tools/run.sh --width 960 --height 540 --play --autoplay tour --tourstart 7 --tourcount 1 --shotdir "Z:\\tmp\\faces\\perf_$2\\" > /tmp/faces/run_perf_$2.log 2>&1
echo "exit $?"
cp $PFX/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/faces/log_perf_$2.txt
grep -E "autoplay tour shot|Game assets built|FATAL|vkd3d|error" /tmp/faces/log_perf_$2.txt | head -8
