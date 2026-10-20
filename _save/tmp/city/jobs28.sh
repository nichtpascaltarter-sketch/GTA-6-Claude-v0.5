#!/bin/bash
# the "before" for the tours: stops 2-5 on nt_dev23b (67e6159 + batch 13), after jobs27.sh's tours on nt_dev23c finish
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs23.status; }
until grep -q "^jobs23 done" /tmp/city/jobs23.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/jobs27.sh" > /dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
st "start tour 2-5 (dev23b, before)"
mkdir -p /tmp/city/tour_dev22b
gate 3300; TIMEOUT=6000 EXE=/tmp/city/nt_dev23b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev22b\' > /tmp/city/tour_dev22b.out 2>&1
cp $L /tmp/city/tour_dev22b_log.txt
st "jobs28 done"
