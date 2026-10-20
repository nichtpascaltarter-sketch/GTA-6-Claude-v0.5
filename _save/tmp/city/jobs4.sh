#!/bin/sh
# my Wine runs: the story regressions (b1, base2)
cd /home/user/GTA-6-Claude-v0.5
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs3.status; }
st "start b1 regression"
TIMEOUT=6000 EXE=/tmp/city/nt_b1.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_b1\' > /tmp/city/reg_b1.out 2>&1
cp $L /tmp/city/reg_b1_log.txt
st "start base2 regression"
TIMEOUT=6000 EXE=/tmp/city/nt_base2.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_base2\' > /tmp/city/reg_base2.out 2>&1
cp $L /tmp/city/reg_base2_log.txt
st "done"
