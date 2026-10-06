#!/bin/bash
# after jobs10: the full 16-stop autoplay tour on nt_dev10.exe (main + batches 5-8), the scorecard's frames
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs11.status; }
until grep -q "jobs10 done\|no dev10 exe\|dev10 syntax errors" /tmp/city/jobs10.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev10.exe ] || { st "no dev10 exe"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/tour_dev10
st "start tour (dev10)"
TIMEOUT=9000 EXE=/tmp/city/nt_dev10.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 0 --tourcount 16 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev10\' > /tmp/city/tour_dev10.out 2>&1
cp $L /tmp/city/tour_dev10_log.txt
st "jobs11 done"
