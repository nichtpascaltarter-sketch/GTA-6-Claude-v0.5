#!/bin/sh
# my Wine runs, one at a time (after the batch-1 views): baseline tour, batch-2 check shots, batch-2 tour
cd /home/user/GTA-6-Claude-v0.5
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs3.status; }
# the batch-1 views game (left running by jobs3.sh)
while kill -0 8036 2>/dev/null; do sleep 15; done
cp $L /tmp/city/b1_shots_log.txt
st "b1 views done"
while ! grep -q "^real" /tmp/city/build_base2.log 2>/dev/null; do sleep 20; done
st "start base2 tour"
TIMEOUT=3000 EXE=/tmp/city/nt_base2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_base2\' > /tmp/city/tour_base2.out 2>&1
cp $L /tmp/city/tour_base2_log.txt
while ! grep -q "^real" /tmp/city/build_b2.log 2>/dev/null; do sleep 20; done
st "start b2 check shots"
TIMEOUT=3600 EXE=/tmp/city/nt_b2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\b2_check\' $(cat /tmp/city/b2_check_args.txt) > /tmp/city/b2_check.out 2>&1
cp $L /tmp/city/b2_check_log.txt
st "start b2 tour"
TIMEOUT=3000 EXE=/tmp/city/nt_b2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_b2\' > /tmp/city/tour_b2.out 2>&1
cp $L /tmp/city/tour_b2_log.txt
st "jobs5 done"
