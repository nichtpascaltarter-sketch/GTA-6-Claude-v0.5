#!/bin/bash
# after jobs9: the batch-5 yard views on nt_dev7.exe in the same order as the "before" run on nt_dev4b.exe (dev7_before_shots),
# for a like-for-like draw count (the after shots ran in a different order, so streaming differed)
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobsAB5.status; }
until grep -q "jobs9 done\|no dev9 exe\|dev9 syntax errors" /tmp/city/jobs9.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/ab5_shots
st "start ab5 shots (dev7)"
TIMEOUT=7200 EXE=/tmp/city/nt_dev7.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\ab5_shots\' $(cat /tmp/city/dev7_before_shot_args.txt) > /tmp/city/ab5_shots.out 2>&1
cp $L /tmp/city/ab5_shots_log.txt
st "ab5 done"
