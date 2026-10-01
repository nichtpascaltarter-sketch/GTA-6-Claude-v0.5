#!/bin/sh
# my Wine runs (after jobs6): the batch-2 views (the 20 district views and the island views)
cd /home/user/GTA-6-Claude-v0.5
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs3.status; }
while ! grep -q "jobs6 done" /tmp/city/jobs3.status 2>/dev/null; do sleep 20; done
st "start b2 views"
TIMEOUT=6000 EXE=/tmp/city/nt_b2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\b2_shots\' $(cat /tmp/city/base_shot_args.txt) > /tmp/city/b2_shots.out 2>&1
cp $L /tmp/city/b2_shots_log.txt
st "jobs7 done"
