#!/bin/bash
# The batch 6 segfault triage (the coordinator's plan): mission 10 (bagman) on its own, three times each on nt_dev9.exe
# (8fb1e66 + batch 6) and on nt_m8.exe (plain main 8fb1e66, built here), alternating; then batch 8 (nt_dev10.exe): its
# 19 shots, its story regression, the same shots on nt_dev9.exe as the "before"
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs10.status; }
st "jobs10c start"
rm -f /tmp/city/nt_m8.exe
(
  mkdir -p $S/m8 && cd /home/user/GTA-6-Claude-v0.5 && git archive 8fb1e66 | tar -x -C $S/m8
  cd $S/m8 && { time OUT=/tmp/city/nt_m8.exe sh ./build.sh ; } > /tmp/city/build_m8.log 2>&1
  echo "m8 build rc=$? $(date)" >> /tmp/city/jobs10.status
) &
until grep -q "jobs9 done" /tmp/city/jobs9.status 2>/dev/null; do sleep 30; done
cd /home/user/GTA-6-Claude-v0.5
for i in 1 2 3; do
  for exe in dev9 m8; do
    if [ $exe = m8 ]; then
      until grep -q "m8 build rc" /tmp/city/jobs10.status 2>/dev/null; do sleep 20; done
      [ -f /tmp/city/nt_m8.exe ] || { st "no m8 exe"; continue; }
    fi
    mkdir -p /tmp/city/bag_${exe}_$i
    st "start bagman $exe #$i"
    TIMEOUT=1800 EXE=/tmp/city/nt_$exe.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest bagman --renderevery 300 --width 960 --height 540 --shotdir "Z:\\tmp\\city\\bag_${exe}_${i}\\" > /tmp/city/bag_${exe}_$i.out 2>&1
    rc=$?
    cp $L /tmp/city/bag_${exe}_${i}_log.txt
    st "bagman $exe #$i rc=$rc: $(grep -c 'PASSED at' /tmp/city/bag_${exe}_${i}_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/bag_${exe}_$i.out) segfaults"
  done
done
st "triage done"
mkdir -p /tmp/city/dev10_shots /tmp/city/dev10_before_shots /tmp/city/reg_dev10
st "start dev10 shots"
TIMEOUT=7200 EXE=/tmp/city/nt_dev10.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev10_shots\' $(cat /tmp/city/dev10_shot_args.txt) > /tmp/city/dev10_shots.out 2>&1
cp $L /tmp/city/dev10_shots_log.txt
st "start dev10 regression"
TIMEOUT=9000 EXE=/tmp/city/nt_dev10.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev10\' > /tmp/city/reg_dev10.out 2>&1
cp $L /tmp/city/reg_dev10_log.txt
st "dev10 regression done: $(grep -c 'PASSED at' /tmp/city/reg_dev10_log.txt) passed, $(grep -c 'Segmentation' /tmp/city/reg_dev10.out) segfaults"
st "start before shots (dev9)"
TIMEOUT=7200 EXE=/tmp/city/nt_dev9.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats --shotdir 'Z:\tmp\city\dev10_before_shots\' $(cat /tmp/city/dev10_shot_args.txt) > /tmp/city/dev10_before_shots.out 2>&1
cp $L /tmp/city/dev10_before_shots_log.txt
st "jobs10 done"
