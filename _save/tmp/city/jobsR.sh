#!/bin/sh
# my Wine runs, detached, after the batch-2 regression: batch-3 check shots, tour, story regression, district views
cd /home/user/GTA-6-Claude-v0.5
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs3.status; }
while ! grep -q "jobsQ done" /tmp/city/jobs3.status 2>/dev/null; do sleep 20; done
while ! grep -q "b3 build done\|b3 syntax errors" /tmp/city/buildq.status 2>/dev/null; do sleep 20; done
if [ ! -f /tmp/city/nt_b3.exe ]; then st "no b3 exe"; exit 1; fi
st "start b3 check shots"
TIMEOUT=5400 EXE=/tmp/city/nt_b3.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\b3_check\' $(cat /tmp/city/b3_check_args.txt) > /tmp/city/b3_check.out 2>&1
cp $L /tmp/city/b3_check_log.txt
st "start b3 tour"
TIMEOUT=4000 EXE=/tmp/city/nt_b3.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_b3\' > /tmp/city/tour_b3.out 2>&1
cp $L /tmp/city/tour_b3_log.txt
st "start b3 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_b3.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_b3\' > /tmp/city/reg_b3.out 2>&1
cp $L /tmp/city/reg_b3_log.txt
st "start b3 views"
TIMEOUT=7200 EXE=/tmp/city/nt_b3.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\b3_shots\' $(cat /tmp/city/base_shot_args.txt) > /tmp/city/b3_shots.out 2>&1
cp $L /tmp/city/b3_shots_log.txt
st "jobsR done"
