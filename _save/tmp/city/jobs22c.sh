#!/bin/bash
# the rest of batch 13's tests: the 3 showcase views at 1920x1080 ultra and the 9 villa / ranch street views, after
# (nt_dev20) and before (nt_dev15); then on nt_dev20b (= nt_dev20 + batch 11b's interiors.cpp, the same world) the story
# regression and tour stops 2-5 and 13; stop 13 on nt_dev15 for the before. One game of mine at a time.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs22.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt 3300 ]; do sleep 20; done; }
mkdir -p /tmp/city/dev20_hq /tmp/city/dev20_hq_before /tmp/city/dev20_item3 /tmp/city/dev20_item3_before /tmp/city/reg_dev20 /tmp/city/tour_dev20 /tmp/city/tour_dev20_grove /tmp/city/tour_dev15_grove
run() {   # exe outdir log args...
  local exe=$1 out=$2 log=$3; shift 3
  gate; TIMEOUT=3600 EXE=$exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh "$@" --shotdir "Z:${out//\//\\}\\" > $log.out 2>&1
  local rc=$?; cp $L $log
  st "$(basename $log) rc=$rc: $(ls $out | wc -l) files, $(grep -c 'Segmentation' $log.out) segfaults"
}
st "jobs22c start"
HQ="--shot 2886.54,-730.50,326.39,-30.00,-30.00,22.00,downtown_aerial_night --shot 4601.40,1372.03,264.22,-75.00,-28.00,18.80,sol_beach_aerial_sunset --shot 1962.67,-3500.00,4.24,-154.82,3.00,17.00,grove_street"
run /tmp/city/nt_dev20.exe /tmp/city/dev20_hq /tmp/city/dev20_hq_log.txt --width 1920 --height 1080 --quality 3 --settle 16 --gfxstats $HQ
run /tmp/city/nt_dev15.exe /tmp/city/dev20_hq_before /tmp/city/dev20_hq_before_log.txt --width 1920 --height 1080 --quality 3 --settle 16 --gfxstats $HQ
run /tmp/city/nt_dev20.exe /tmp/city/dev20_item3 /tmp/city/dev20_item3_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev20_item3_args.txt)
run /tmp/city/nt_dev15.exe /tmp/city/dev20_item3_before /tmp/city/dev20_item3_before_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev20_item3_args.txt)
until grep -q "dev20b exe ready\|no dev20b exe\|dev20b syntax errors" /tmp/city/cq20e.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev20b.exe ] || { st "no dev20b exe"; exit 1; }
st "start dev20b regression"
gate; TIMEOUT=9000 EXE=/tmp/city/nt_dev20b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev20\' > /tmp/city/reg_dev20.out 2>&1
cp $L /tmp/city/reg_dev20_log.txt; rm -rf /tmp/city/reg_dev20
st "dev20b regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev20_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev20.out) segfaults"
st "start tour 2-5 (dev20b)"
gate; TIMEOUT=6000 EXE=/tmp/city/nt_dev20b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev20\' > /tmp/city/tour_dev20.out 2>&1
cp $L /tmp/city/tour_dev20_log.txt
st "start tour 13 (dev20b)"
gate; TIMEOUT=3000 EXE=/tmp/city/nt_dev20b.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 13 --tourcount 1 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev20_grove\' > /tmp/city/tour_dev20_grove.out 2>&1
cp $L /tmp/city/tour_dev20_grove_log.txt
st "start tour 13 (dev15, before)"
gate; TIMEOUT=3000 EXE=/tmp/city/nt_dev15.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 13 --tourcount 1 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev15_grove\' > /tmp/city/tour_dev15_grove.out 2>&1
cp $L /tmp/city/tour_dev15_grove_log.txt
st "jobs22 done"
