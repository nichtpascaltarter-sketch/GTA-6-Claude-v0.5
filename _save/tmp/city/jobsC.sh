#!/bin/bash
# after jobsB: the district views the timed-out shot run missed, and two check shots again with the bougainvillea fix,
# on nt_dev4b.exe (batch 4 on 8ee5699)
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobsC.status; }
st "start"
while ! grep -q "jobsB done" /tmp/city/jobsB.status 2>/dev/null; do sleep 30; done
while [ ! -s /tmp/city/dev4b_extra_args.txt ]; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev4b_shots
st "start dev4b extra shots"
TIMEOUT=7200 EXE=/tmp/city/nt_dev4b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\dev4b_shots\' $(cat /tmp/city/dev4b_extra_args.txt) > /tmp/city/dev4b_shots.out 2>&1
cp $L /tmp/city/dev4b_shots_log.txt
st "jobsC done"
