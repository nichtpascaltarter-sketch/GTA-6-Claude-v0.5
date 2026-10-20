#!/bin/sh
# my Wine runs, one at a time: wait for the baseline shots, then the baseline regression and tour stops 2-5
cd /home/user/GTA-6-Claude-v0.5
while ! grep -q "^exit" /tmp/city/base_shots2.log 2>/dev/null; do sleep 30; done
echo "start regression $(date)" > /tmp/city/jobs_base.status
TIMEOUT=5400 EXE=/tmp/city/nt_base.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_base\' > /tmp/city/reg_base.out 2>&1
cp /tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/city/reg_base_log.txt
echo "start tour $(date)" >> /tmp/city/jobs_base.status
TIMEOUT=3000 EXE=/tmp/city/nt_base.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_base\' > /tmp/city/tour_base.out 2>&1
cp /tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/city/tour_base_log.txt
echo "done $(date)" >> /tmp/city/jobs_base.status
