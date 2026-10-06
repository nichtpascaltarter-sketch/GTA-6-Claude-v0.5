#!/bin/bash
# batch 3 sign-off on main 06580c3: the story regression (27 missions), then the 20 district views, on nt_base4.exe
cd /home/user/GTA-6-Claude-v0.5
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobsX.status; }
st "start"
while ! grep -q "^rc=\|real" /tmp/city/build_base4.log 2>/dev/null; do sleep 20; done
if [ ! -f /tmp/city/nt_base4.exe ]; then st "no base4 exe"; exit 1; fi
st "start base4 regression"
mkdir -p /tmp/city/reg_base4 /tmp/city/base4_shots
TIMEOUT=9000 EXE=/tmp/city/nt_base4.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_base4\' > /tmp/city/reg_base4.out 2>&1
cp $L /tmp/city/reg_base4_log.txt
st "start base4 views"
TIMEOUT=7200 EXE=/tmp/city/nt_base4.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --shotdir 'Z:\tmp\city\base4_shots\' $(cat /tmp/city/base_shot_args.txt) > /tmp/city/base4_shots.out 2>&1
cp $L /tmp/city/base4_shots_log.txt
st "jobsX done"
