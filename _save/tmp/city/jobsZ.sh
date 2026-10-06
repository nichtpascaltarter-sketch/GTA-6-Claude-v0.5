#!/bin/bash
# batch 4 game runs (after the batch-3 runs): dev4 check shots + the 20 district views, dev4 story regression, the tour
# stops 2-5 on base4 (batch 3 on 06580c3) and on dev4
cd /home/user/GTA-6-Claude-v0.5
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobsZ.status; }
st "start"
while ! grep -q "jobsX done" /tmp/city/jobsX.status 2>/dev/null; do sleep 30; done
while ! grep -q "dev4 build rc=\|dev4 syntax errors\|citylab build failed" /tmp/city/jobsY.status 2>/dev/null; do sleep 30; done
if [ ! -f /tmp/city/nt_dev4.exe ]; then st "no dev4 exe"; exit 1; fi
mkdir -p /tmp/city/dev4_shots /tmp/city/reg_dev4 /tmp/city/tour_base4 /tmp/city/tour_dev4
st "start dev4 shots"
TIMEOUT=7200 EXE=/tmp/city/nt_dev4.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\dev4_shots\' $(cat /tmp/city/dev4_check_args.txt) $(cat /tmp/city/base_shot_args.txt) > /tmp/city/dev4_shots.out 2>&1
cp $L /tmp/city/dev4_shots_log.txt
st "start dev4 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev4.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev4\' > /tmp/city/reg_dev4.out 2>&1
cp $L /tmp/city/reg_dev4_log.txt
st "start base4 tour"
TIMEOUT=4500 EXE=/tmp/city/nt_base4.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_base4\' > /tmp/city/tour_base4.out 2>&1
cp $L /tmp/city/tour_base4_log.txt
st "start dev4 tour"
TIMEOUT=4500 EXE=/tmp/city/nt_dev4.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev4\' > /tmp/city/tour_dev4.out 2>&1
cp $L /tmp/city/tour_dev4_log.txt
st "jobsZ done"
