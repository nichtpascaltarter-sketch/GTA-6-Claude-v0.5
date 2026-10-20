#!/bin/sh
# my Wine runs, one at a time: wait for the baseline shots; a visual check of the dev build; the baseline regression and tour
cd /home/user/GTA-6-Claude-v0.5
while [ ! -f /tmp/city/base_shots2_game.log ]; do sleep 30; done
echo "start devcheck $(date)" > /tmp/city/jobs2.status
ARGS=$(cat /tmp/city/dev_check_shots.txt | tr '\n' ' ')
TIMEOUT=3600 EXE=/tmp/city/nt_dev.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\dev_shots\' $ARGS > /tmp/city/dev_shots.out 2>&1
cp /tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/city/dev_shots_log.txt
echo "start regression $(date)" >> /tmp/city/jobs2.status
TIMEOUT=5400 EXE=/tmp/city/nt_base.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_base\' > /tmp/city/reg_base.out 2>&1
cp /tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/city/reg_base_log.txt
echo "start tour $(date)" >> /tmp/city/jobs2.status
TIMEOUT=3000 EXE=/tmp/city/nt_base.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_base\' > /tmp/city/tour_base.out 2>&1
cp /tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt /tmp/city/tour_base_log.txt
echo "done $(date)" >> /tmp/city/jobs2.status
