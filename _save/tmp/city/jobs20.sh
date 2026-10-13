#!/bin/bash
# batch 13 (roofs for the aerial views: roof plant, copings, antennas, water tanks, balcony slabs; ranch colours and
# porches, Key Coral villa forms) on nt_dev20 = nt_dev15 + the change: after jobs15 (one game at a time). Shots after and
# before (nt_dev15), the showcase views at 1920x1080 ultra after and before, the story regression, tour stops 2-5 and 13.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs20.status; }
st "waiting for jobs15 and nt_dev20"
until grep -q "jobs15 done" /tmp/city/jobs15.status 2>/dev/null; do sleep 30; done
until grep -q "dev20 exe ready\|no dev20 exe\|dev20 syntax errors\|dev20 host syntax errors" /tmp/city/cq20.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev20.exe ] || { st "no dev20 exe"; exit 1; }
until [ -f /tmp/city/dev20_shot_args.txt ]; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev20_shots /tmp/city/dev20_before_shots /tmp/city/dev20_hq /tmp/city/dev20_hq_before /tmp/city/reg_dev20 /tmp/city/tour_dev20 /tmp/city/tour_dev20_grove
run() {   # exe outdir log args...
  local exe=$1 out=$2 log=$3; shift 3
  TIMEOUT=3600 EXE=$exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh "$@" --shotdir "Z:${out//\//\\}\\" > $log.out 2>&1
  local rc=$?; cp $L $log
  st "$(basename $log) rc=$rc: $(ls $out | wc -l) files, $(grep -c 'Segmentation' $log.out) segfaults"
}
run /tmp/city/nt_dev20.exe /tmp/city/dev20_shots /tmp/city/dev20_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev20_shot_args.txt)
run /tmp/city/nt_dev15.exe /tmp/city/dev20_before_shots /tmp/city/dev20_before_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev20_shot_args.txt)
HQ="--shot 2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night --shot 4601.40,1372.03,264.22,-75.00,-28.00,18.80,sol_beach_aerial_sunset --shot 1962.67,-3500.00,4.24,-154.82,3.00,17.00,grove_street"
run /tmp/city/nt_dev20.exe /tmp/city/dev20_hq /tmp/city/dev20_hq_log.txt --width 1920 --height 1080 --quality 3 --settle 16 --gfxstats $HQ
run /tmp/city/nt_dev15.exe /tmp/city/dev20_hq_before /tmp/city/dev20_hq_before_log.txt --width 1920 --height 1080 --quality 3 --settle 16 --gfxstats $HQ
st "start dev20 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev20.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev20\' > /tmp/city/reg_dev20.out 2>&1
cp $L /tmp/city/reg_dev20_log.txt
st "dev20 regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev20_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev20.out) segfaults"
st "start tour 2-5 (dev20)"
TIMEOUT=6000 EXE=/tmp/city/nt_dev20.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev20\' > /tmp/city/tour_dev20.out 2>&1
cp $L /tmp/city/tour_dev20_log.txt
st "start tour 13 (dev20)"
TIMEOUT=3000 EXE=/tmp/city/nt_dev20.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 13 --tourcount 1 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev20_grove\' > /tmp/city/tour_dev20_grove.out 2>&1
cp $L /tmp/city/tour_dev20_grove_log.txt
st "jobs20 done"
