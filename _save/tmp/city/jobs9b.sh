#!/bin/bash
# batch 6 (dev9 = 8fb1e66 + batch 6, nt_dev9.exe) after the container restart: the remaining shots, the story regression,
# then the before shots on nt_dev7.exe (8fb1e66's world = batch 5)
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs9.status; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev9_shots /tmp/city/dev9_before_shots /tmp/city/reg_dev9
st "restart: remaining dev9 shots"
TIMEOUT=7200 EXE=/tmp/city/nt_dev9.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev9_shots\' $(cat /tmp/city/dev9_shot_args_rest.txt) > /tmp/city/dev9_shots.out 2>&1
cp $L /tmp/city/dev9_shots_log.txt
st "start dev9 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev9.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev9\' > /tmp/city/reg_dev9.out 2>&1
cp $L /tmp/city/reg_dev9_log.txt
st "start before shots (dev7)"
TIMEOUT=7200 EXE=/tmp/city/nt_dev7.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev9_before_shots\' $(cat /tmp/city/dev9_before_shot_args.txt) > /tmp/city/dev9_before_shots.out 2>&1
cp $L /tmp/city/dev9_before_shots_log.txt
st "jobs9 done"
