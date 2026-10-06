#!/bin/bash
# after jobsD: the batch-6 yard shots on nt_dev6.exe (after) and nt_dev4b.exe (before)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs6b.status; }
st "start (waiting for jobsD and the dev6 exe)"
until grep -q "jobsD done\|no dev4b" /tmp/city/jobsD.status 2>/dev/null; do sleep 30; done
until grep -q "dev6 exe ready\|no dev6 exe\|syntax errors" /tmp/city/jobs6.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
if [ -f /tmp/city/nt_dev6.exe ]; then
  mkdir -p /tmp/city/dev6_shots
  st "start dev6 shots"
  TIMEOUT=7200 EXE=/tmp/city/nt_dev6.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\dev6_shots\' $(cat /tmp/city/dev6_shot_args.txt) > /tmp/city/dev6_shots.out 2>&1
  cp $L /tmp/city/dev6_shots_log.txt
fi
mkdir -p /tmp/city/dev6_before_shots
st "start before shots (dev4b)"
TIMEOUT=7200 EXE=/tmp/city/nt_dev4b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\dev6_before_shots\' $(cat /tmp/city/dev6_shot_args.txt) > /tmp/city/dev6_before_shots.out 2>&1
cp $L /tmp/city/dev6_before_shots_log.txt
st "jobs6b done"
