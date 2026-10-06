#!/bin/sh
# my Wine runs, detached: finish the batch-2 tour (running), then the batch-2 story regression
cd /home/user/GTA-6-Claude-v0.5
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs3.status; }
while kill -0 17078 2>/dev/null; do sleep 15; done
cp $L /tmp/city/tour_b2_log.txt
st "b2 tour done"
st "start b2 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_b2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_b2\' > /tmp/city/reg_b2.out 2>&1
cp $L /tmp/city/reg_b2_log.txt
st "jobsQ done"
