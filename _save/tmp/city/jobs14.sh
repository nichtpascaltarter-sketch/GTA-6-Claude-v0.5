#!/bin/bash
# batch 10 (storefront glass stopgap dropped, on main b907098): after jobs13b's tours and
# nt_dev14's build: 10 storefront shots on nt_dev14, the same on nt_dev13 (before: the stopgap, the old shader), then the
# story regression on nt_dev14. One game at a time.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs14.status; }
st "waiting for jobs13b and the dev14 build"
until grep -q "jobs13 done" /tmp/city/jobs10.status 2>/dev/null; do sleep 30; done
until grep -q "dev14b exe ready\|no dev14b exe\|dev14b syntax errors" /tmp/city/cq14.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev14.exe ] || { st "no dev14 exe"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev14_shots /tmp/city/dev14_before_shots /tmp/city/reg_dev14
st "start dev14 shots"
TIMEOUT=3600 EXE=/tmp/city/nt_dev14.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev14_shots\' $(cat /tmp/city/dev14_shot_args.txt) > /tmp/city/dev14_shots.out 2>&1
cp $L /tmp/city/dev14_shots_log.txt
st "start before shots (main b907098)"
until grep -q "m10 exe ready\|no m10 exe" /tmp/city/cq14.status 2>/dev/null; do sleep 30; done
B=/tmp/city/nt_m10.exe; [ -f $B ] || B=/tmp/city/nt_dev13.exe
TIMEOUT=3600 EXE=$B WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev14_before_shots\' $(cat /tmp/city/dev14_shot_args.txt) > /tmp/city/dev14_before_shots.out 2>&1
cp $L /tmp/city/dev14_before_shots_log.txt
st "start dev14 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev14.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev14\' > /tmp/city/reg_dev14.out 2>&1
cp $L /tmp/city/reg_dev14_log.txt
st "dev14 regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev14_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev14.out) segfaults"
st "jobs14 done"
