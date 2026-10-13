#!/bin/bash
# after jobs7: tour stop 5 (Sol Beach condos) again on main (nt_m6.exe) and batch 4 (nt_dev4b.exe) - the first pair
# differed by 115 draws there with unchanged building geometry
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/tour5.status; }
until grep -q "jobs7 done" /tmp/city/jobs7.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/tour5_m6 /tmp/city/tour5_dev4b
st "start m6 stop 5"
TIMEOUT=3600 EXE=/tmp/city/nt_m6.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 5 --tourcount 1 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour5_m6\' > /tmp/city/tour5_m6.out 2>&1
cp $L /tmp/city/tour5_m6_log.txt
st "start dev4b stop 5"
TIMEOUT=3600 EXE=/tmp/city/nt_dev4b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 5 --tourcount 1 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour5_dev4b\' > /tmp/city/tour5_dev4b.out 2>&1
cp $L /tmp/city/tour5_dev4b_log.txt
st "tour5 done"
