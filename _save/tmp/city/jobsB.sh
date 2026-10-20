#!/bin/bash
# batch 4 on main 8ee5699: build dev4 and main, then (after the running dev4 shot run) the story regression on dev4 and
# the tour stops 2-5 on main (before) and dev4 (after)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobsB.status; }
st "start"
cd $S/dev4 && { time OUT=/tmp/city/nt_dev4b.exe sh ./build.sh ; } > /tmp/city/build_dev4b.log 2>&1; st "dev4b build rc=$?"
cd $S/m6 && { time OUT=/tmp/city/nt_m6.exe sh ./build.sh ; } > /tmp/city/build_m6.log 2>&1; st "m6 build rc=$?"
while kill -0 26990 2>/dev/null; do sleep 20; done
cp $L /tmp/city/dev4_shots_log.txt
st "dev4 shots done ($(ls /tmp/city/dev4_shots | wc -l) shots)"
cd /home/user/GTA-6-Claude-v0.5
if [ ! -f /tmp/city/nt_dev4b.exe ]; then st "no dev4b exe"; exit 1; fi
mkdir -p /tmp/city/reg_dev4b /tmp/city/tour_m6 /tmp/city/tour_dev4b
st "start dev4b regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev4b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev4b\' > /tmp/city/reg_dev4b.out 2>&1
cp $L /tmp/city/reg_dev4b_log.txt
st "start m6 tour"
TIMEOUT=4500 EXE=/tmp/city/nt_m6.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_m6\' > /tmp/city/tour_m6.out 2>&1
cp $L /tmp/city/tour_m6_log.txt
st "start dev4b tour"
TIMEOUT=4500 EXE=/tmp/city/nt_dev4b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev4b\' > /tmp/city/tour_dev4b.out 2>&1
cp $L /tmp/city/tour_dev4b_log.txt
st "jobsB done"
