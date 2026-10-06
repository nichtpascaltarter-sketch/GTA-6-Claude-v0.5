#!/bin/sh
# my Wine runs, one at a time: batch-1 views, tours (b1, base2), story regressions (b1, base2)
cd /home/user/GTA-6-Claude-v0.5
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs3.status; }
while ! grep -q "^real" /tmp/city/build_b1.log 2>/dev/null; do sleep 20; done
[ -f /tmp/city/nt_b1.exe ] || { st "no b1 exe"; exit 1; }
st "start b1 views"
TIMEOUT=3600 EXE=/tmp/city/nt_b1.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\b1_shots\' $(cat /tmp/city/base_shot_args.txt) > /tmp/city/b1_shots.out 2>&1
cp $L /tmp/city/b1_shots_log.txt
st "start b1 tour"
TIMEOUT=3000 EXE=/tmp/city/nt_b1.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_b1\' > /tmp/city/tour_b1.out 2>&1
cp $L /tmp/city/tour_b1_log.txt
while ! grep -q "^real" /tmp/city/build_base2.log 2>/dev/null; do sleep 20; done
st "start base2 tour"
TIMEOUT=3000 EXE=/tmp/city/nt_base2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_base2\' > /tmp/city/tour_base2.out 2>&1
cp $L /tmp/city/tour_base2_log.txt
st "tours done"
