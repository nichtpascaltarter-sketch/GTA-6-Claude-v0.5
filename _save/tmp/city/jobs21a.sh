#!/bin/bash
# batch 11 on nt_dev15: tour stops 2-5 (the rest of its tests passed on 10-06). One game of mine at a time.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs21.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt 3300 ]; do sleep 20; done; }
mkdir -p /tmp/city/tour_dev15
st "start tour 2-5 (dev15)"
gate; TIMEOUT=6000 EXE=/tmp/city/nt_dev15.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev15\' > /tmp/city/tour_dev15.out 2>&1
rc=$?; cp $L /tmp/city/tour_dev15_log.txt
st "b11 tours done rc=$rc: $(grep -c 'autoplay tour shot' /tmp/city/tour_dev15_log.txt) stops, $(grep -c 'Segmentation' /tmp/city/tour_dev15.out) segfaults"
