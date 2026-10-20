#!/bin/bash
# Takes over from jobs10c after its first triage run (bagman on nt_dev9, PID 26756): the rest of the bagman triage
# (nt_m8 = main 8fb1e66, nt_dev9 = 8fb1e66 + batch 6), then the combined batches 6 + 8 + 9 on nt_dev13 (main d3c2c3f +
# the world files): 43 shots, the story regression, then tour stops 2-5 (frame cost) and 13-15. One game at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs10.status; }
st "jobs13b takes over (waiting for bagman m8 #2)"
while kill -0 7056 2>/dev/null; do sleep 10; done
cp $L /tmp/city/bag_m8_2_log.txt
st "bagman m8 #2 done: $(grep -c 'PASSED at' /tmp/city/bag_m8_2_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/bag_m8_2.out) segfaults"
cd /home/user/GTA-6-Claude-v0.5
runbag() {
  local exe=$1 i=$2
  mkdir -p /tmp/city/bag_${exe}_$i
  st "start bagman $exe #$i"
  TIMEOUT=1800 EXE=/tmp/city/nt_$exe.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest bagman --renderevery 300 --width 960 --height 540 --shotdir "Z:\\tmp\\city\\bag_${exe}_${i}\\" > /tmp/city/bag_${exe}_$i.out 2>&1
  local rc=$?
  cp $L /tmp/city/bag_${exe}_${i}_log.txt
  st "bagman $exe #$i rc=$rc: $(grep -c 'PASSED at' /tmp/city/bag_${exe}_${i}_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/bag_${exe}_$i.out) segfaults"
}
runbag dev9 3; runbag m8 3
st "triage done"
until grep -q "dev13b exe ready\|no dev13b exe\|dev13b syntax errors" /tmp/city/cq13.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev13.exe ] || { st "no dev13 exe"; exit 1; }
mkdir -p /tmp/city/dev13_shots /tmp/city/reg_dev13 /tmp/city/tour_dev13 /tmp/city/tour_dev13_towns
st "start dev13 shots"
TIMEOUT=7200 EXE=/tmp/city/nt_dev13.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev13_shots\' $(cat /tmp/city/dev13_shot_args.txt) > /tmp/city/dev13_shots.out 2>&1
cp $L /tmp/city/dev13_shots_log.txt
st "dev13 shots done: $(ls /tmp/city/dev13_shots | wc -l) of 43, $(grep -c 'Segmentation' /tmp/city/dev13_shots.out) segfaults"
st "start dev13 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev13.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev13\' > /tmp/city/reg_dev13.out 2>&1
cp $L /tmp/city/reg_dev13_log.txt
st "dev13 regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev13_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev13.out) segfaults"
st "start tour 2-5 (dev13)"
TIMEOUT=6000 EXE=/tmp/city/nt_dev13.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev13\' > /tmp/city/tour_dev13.out 2>&1
cp $L /tmp/city/tour_dev13_log.txt
st "start tour 13-15 (dev13)"
TIMEOUT=4800 EXE=/tmp/city/nt_dev13.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 13 --tourcount 3 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev13_towns\' > /tmp/city/tour_dev13_towns.out 2>&1
cp $L /tmp/city/tour_dev13_towns_log.txt
st "jobs13 done"
