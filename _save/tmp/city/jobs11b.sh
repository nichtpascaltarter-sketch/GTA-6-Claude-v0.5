#!/bin/bash
# after jobs10: tour stops 2-5 on nt_dev10.exe (batch 8's frame cost at the scorecard stops 2, 3, 5), then the town
# stops 13-15 (Grove suburb, Okahatchee, Fort Castell): one run per slice so a slice's log survives the next
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs11.status; }
until grep -q "jobs10 done\|no dev10 exe\|dev10 syntax errors" /tmp/city/jobs10.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev10.exe ] || { st "no dev10 exe"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/tour_dev10 /tmp/city/tour_dev10_towns
st "start tour 2-5 (dev10)"
TIMEOUT=6000 EXE=/tmp/city/nt_dev10.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev10\' > /tmp/city/tour_dev10.out 2>&1
cp $L /tmp/city/tour_dev10_log.txt
st "tour 2-5 done; start tour 13-15 (dev10)"
TIMEOUT=4800 EXE=/tmp/city/nt_dev10.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 13 --tourcount 3 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev10_towns\' > /tmp/city/tour_dev10_towns.out 2>&1
cp $L /tmp/city/tour_dev10_towns_log.txt
st "jobs11 done"
