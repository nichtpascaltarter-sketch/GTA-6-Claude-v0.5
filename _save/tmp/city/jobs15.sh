#!/bin/bash
# batch 11 (garage wings kept on their lots) on nt_dev15 = main b907098 + batch 10 + the fix: shots after and before
# (nt_dev14), the North Porto Sol showcase shot at 1920x1080 ultra after and before, the story regression, tour stops 2-5.
# One game at a time.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs15.status; }
st "waiting for nt_dev15"
until grep -q "dev15 exe ready\|no dev15 exe\|dev15 syntax errors" /tmp/city/cq15.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev15.exe ] || { st "no dev15 exe"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
mkdir -p /tmp/city/dev15_shots /tmp/city/dev15_before_shots /tmp/city/dev15_hq /tmp/city/dev15_hq_before /tmp/city/reg_dev15 /tmp/city/tour_dev15
run() {   # exe outdir log args...
  local exe=$1 out=$2 log=$3; shift 3
  TIMEOUT=3600 EXE=$exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh "$@" --shotdir "Z:${out//\//\\}\\" > $log.out 2>&1
  local rc=$?; cp $L $log
  st "$(basename $log) rc=$rc: $(ls $out | wc -l) files, $(grep -c 'Segmentation' $log.out) segfaults"
}
run /tmp/city/nt_dev15.exe /tmp/city/dev15_shots /tmp/city/dev15_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev15_shot_args.txt)
run /tmp/city/nt_dev14.exe /tmp/city/dev15_before_shots /tmp/city/dev15_before_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev15_shot_args.txt)
run /tmp/city/nt_dev15.exe /tmp/city/dev15_hq /tmp/city/dev15_hq_log.txt --width 1920 --height 1080 --quality 3 --settle 16 --shot 2607.10,4300.00,4.82,-25.00,3.00,11.00,north_porto_sol_street
run /tmp/city/nt_dev14.exe /tmp/city/dev15_hq_before /tmp/city/dev15_hq_before_log.txt --width 1920 --height 1080 --quality 3 --settle 16 --shot 2607.10,4300.00,4.82,-25.00,3.00,11.00,north_porto_sol_street
st "start dev15 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev15.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev15\' > /tmp/city/reg_dev15.out 2>&1
cp $L /tmp/city/reg_dev15_log.txt
st "dev15 regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev15_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev15.out) segfaults"
st "start stick-up encounter (dev15)"
mkdir -p /tmp/city/enc_dev15
TIMEOUT=3600 EXE=/tmp/city/nt_dev15.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest enc_stickup,enc_stickup_b --renderevery 60 --width 960 --height 540 --shotdir 'Z:\tmp\city\enc_dev15\' > /tmp/city/enc_dev15.out 2>&1
cp $L /tmp/city/enc_dev15_log.txt
st "dev15 stick-up done: $(grep -c 'PASSED at' /tmp/city/enc_dev15_log.txt) passed, $(grep -c 'FAILED' /tmp/city/enc_dev15_log.txt) failed, $(grep -c 'Segmentation' /tmp/city/enc_dev15.out) segfaults"
st "start tour 2-5 (dev15)"
TIMEOUT=6000 EXE=/tmp/city/nt_dev15.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev15\' > /tmp/city/tour_dev15.out 2>&1
cp $L /tmp/city/tour_dev15_log.txt
st "jobs15 done"
