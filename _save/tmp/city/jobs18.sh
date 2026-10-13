#!/bin/bash
# batch 13 (roofs for the aerial views: roof plant, parapet copings, antennas, water tanks; balcony slabs on the
# residential towers and condos; soft trash bags) on nt_dev18 = nt_dev17 + the change: after jobs17. Shots after and
# before (nt_dev17) incl. the showcase aerials at 1920x1080 ultra, the story regression, tour stops 2-5.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs18.status; }
st "waiting for jobs17 and nt_dev18"
until grep -q "jobs17 done" /tmp/city/jobs17.status 2>/dev/null; do sleep 30; done
until grep -q "dev18 exe ready\|no dev18 exe\|dev18 syntax errors" /tmp/city/cqchain2.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev18.exe ] || { st "no dev18 exe"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev18_shots /tmp/city/dev18_before_shots /tmp/city/dev18_hq /tmp/city/dev18_hq_before /tmp/city/reg_dev18 /tmp/city/tour_dev18
run() {   # exe outdir log args...
  local exe=$1 out=$2 log=$3; shift 3
  TIMEOUT=3600 EXE=$exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh "$@" --shotdir "Z:${out//\//\\}\\" > $log.out 2>&1
  local rc=$?; cp $L $log
  st "$(basename $log) rc=$rc: $(ls $out | wc -l) files, $(grep -c 'Segmentation' $log.out) segfaults"
}
run /tmp/city/nt_dev18.exe /tmp/city/dev18_shots /tmp/city/dev18_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev18_shot_args.txt)
run /tmp/city/nt_dev17.exe /tmp/city/dev18_before_shots /tmp/city/dev18_before_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev18_shot_args.txt)
HQ="--shot 2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night --shot 4601.40,1372.03,264.22,-75.00,-28.00,18.80,sol_beach_aerial_sunset --shot 2607.10,4300.00,4.82,-25.00,3.00,11.00,north_porto_sol_street"
run /tmp/city/nt_dev18.exe /tmp/city/dev18_hq /tmp/city/dev18_hq_log.txt --width 1920 --height 1080 --quality 3 --settle 16 --gfxstats $HQ
run /tmp/city/nt_dev17.exe /tmp/city/dev18_hq_before /tmp/city/dev18_hq_before_log.txt --width 1920 --height 1080 --quality 3 --settle 16 --gfxstats $HQ
st "start dev18 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev18.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev18\' > /tmp/city/reg_dev18.out 2>&1
cp $L /tmp/city/reg_dev18_log.txt
st "dev18 regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev18_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev18.out) segfaults"
st "start tour 2-5 (dev18)"
TIMEOUT=6000 EXE=/tmp/city/nt_dev18.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev18\' > /tmp/city/tour_dev18.out 2>&1
cp $L /tmp/city/tour_dev18_log.txt
st "jobs18 done"
