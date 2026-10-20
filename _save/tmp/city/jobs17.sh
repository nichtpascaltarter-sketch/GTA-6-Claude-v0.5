#!/bin/bash
# batch 12 (storefront windows kept to the street walls: buildmesh marks + facade.hlsli) on nt_dev17 = nt_dev15 + the
# change: after jobs15 (one game at a time). Shots after (nt_dev17) and before (nt_dev15), day and night, the story
# regression, tour stops 2-5.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs17.status; }
st "waiting for jobs15 and nt_dev17"
until grep -q "jobs15 done" /tmp/city/jobs15.status 2>/dev/null; do sleep 30; done
until grep -q "dev17 exe ready\|no dev17 exe\|dev17 syntax errors" /tmp/city/cqchain.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev17.exe ] || { st "no dev17 exe"; exit 1; }
[ -f /tmp/city/dev17_shot_args.txt ] || { st "no shot list"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev17_shots /tmp/city/dev17_before_shots /tmp/city/reg_dev17 /tmp/city/tour_dev17
run() {   # exe outdir log args...
  local exe=$1 out=$2 log=$3; shift 3
  TIMEOUT=3600 EXE=$exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh "$@" --shotdir "Z:${out//\//\\}\\" > $log.out 2>&1
  local rc=$?; cp $L $log
  st "$(basename $log) rc=$rc: $(ls $out | wc -l) files, $(grep -c 'Segmentation' $log.out) segfaults"
}
run /tmp/city/nt_dev17.exe /tmp/city/dev17_shots /tmp/city/dev17_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev17_shot_args.txt)
run /tmp/city/nt_dev15.exe /tmp/city/dev17_before_shots /tmp/city/dev17_before_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev17_shot_args.txt)
st "start dev17 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev17.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev17\' > /tmp/city/reg_dev17.out 2>&1
cp $L /tmp/city/reg_dev17_log.txt
st "dev17 regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev17_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev17.out) segfaults"
st "start tour 2-5 (dev17)"
TIMEOUT=6000 EXE=/tmp/city/nt_dev17.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev17\' > /tmp/city/tour_dev17.out 2>&1
cp $L /tmp/city/tour_dev17_log.txt
st "jobs17 done"
