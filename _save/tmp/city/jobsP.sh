#!/bin/sh
# my Wine runs, one at a time, detached: finish the baseline tour (running), batch-2 check shots, batch-2 tour,
# batch-2 story regression, batch-2 district views (a baseline regression only for missions that fail in batch 2)
cd /home/user/GTA-6-Claude-v0.5
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs3.status; }
while kill -0 19018 2>/dev/null; do sleep 15; done
cp $L /tmp/city/tour_base2_log.txt
st "base2 tour done"
while ! grep -q "^real" /tmp/city/build_b2.log 2>/dev/null; do sleep 20; done
if [ ! -f /tmp/city/nt_b2.exe ]; then st "no b2 exe"; exit 1; fi
st "start b2 check shots"
TIMEOUT=3600 EXE=/tmp/city/nt_b2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\b2_check\' $(cat /tmp/city/b2_check_args.txt) > /tmp/city/b2_check.out 2>&1
cp $L /tmp/city/b2_check_log.txt
st "start b2 tour"
TIMEOUT=3000 EXE=/tmp/city/nt_b2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_b2\' > /tmp/city/tour_b2.out 2>&1
cp $L /tmp/city/tour_b2_log.txt
st "start b2 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_b2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_b2\' > /tmp/city/reg_b2.out 2>&1
cp $L /tmp/city/reg_b2_log.txt
st "start b2 views"
TIMEOUT=7200 EXE=/tmp/city/nt_b2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\b2_shots\' $(cat /tmp/city/base_shot_args.txt) > /tmp/city/b2_shots.out 2>&1
cp $L /tmp/city/b2_shots_log.txt
st "jobsP done"
