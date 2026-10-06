#!/bin/bash
# after jobs9 (batch 6 before shots): the batch 6 story regression on nt_dev9.exe AGAIN (the first run, 01:31, ended in a
# segfault in bagman, mission 10 of 27, after 9 passes), then batch 8 (nt_dev10.exe): its 19 shots, its regression, and
# the same shots on nt_dev9.exe as the "before"
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs10.status; }
until grep -q "jobs9 done" /tmp/city/jobs9.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/reg_dev9b /tmp/city/dev10_shots /tmp/city/dev10_before_shots /tmp/city/reg_dev10
st "start dev9 regression (2nd)"
TIMEOUT=9000 EXE=/tmp/city/nt_dev9.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev9b\' > /tmp/city/reg_dev9b.out 2>&1
cp $L /tmp/city/reg_dev9b_log.txt
st "dev9 regression (2nd) done: $(grep -c 'PASSED at' /tmp/city/reg_dev9b_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev9b.out) segfaults"
st "start dev10 shots"
TIMEOUT=7200 EXE=/tmp/city/nt_dev10.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev10_shots\' $(cat /tmp/city/dev10_shot_args.txt) > /tmp/city/dev10_shots.out 2>&1
cp $L /tmp/city/dev10_shots_log.txt
st "start dev10 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev10.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev10\' > /tmp/city/reg_dev10.out 2>&1
cp $L /tmp/city/reg_dev10_log.txt
st "dev10 regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev10_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev10.out) segfaults"
st "start before shots (dev9)"
TIMEOUT=7200 EXE=/tmp/city/nt_dev9.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev10_before_shots\' $(cat /tmp/city/dev10_shot_args.txt) > /tmp/city/dev10_before_shots.out 2>&1
cp $L /tmp/city/dev10_before_shots_log.txt
st "jobs10 done"
