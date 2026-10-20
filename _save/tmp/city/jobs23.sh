#!/bin/bash
# batches 12 + 14 + 15 (storefront windows only on street walls; soft trash bags; aviation beacons) on nt_dev23 = HEAD 67e6159 + batch 13 + the change; before: nt_dev23b = HEAD + batch 13, after
# batch 13's tests (one game at a time): 19 shots after/before (side and back walls by day and night, street fronts, the
# bag piles), the story regression, tour stops 2-5 (prop triangles per frame for the bags).
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs23.status; }
st "waiting for jobs22 and nt_dev22"
until grep -q "jobs22e done" /tmp/city/jobs22.status 2>/dev/null; do sleep 30; done
until grep -q "dev23 exe ready\|no dev23 exe\|dev23 syntax errors" /tmp/city/cq23.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev23.exe ] || { st "no dev23 exe"; exit 1; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt 3300 ]; do sleep 20; done; }
mkdir -p /tmp/city/dev22_shots /tmp/city/dev22_before_shots /tmp/city/reg_dev22 /tmp/city/tour_dev22
run() {   # exe outdir log args...
  local exe=$1 out=$2 log=$3; shift 3
  gate; TIMEOUT=3600 EXE=$exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh "$@" --shotdir "Z:${out//\//\\}\\" > $log.out 2>&1
  local rc=$?; cp $L $log
  st "$(basename $log) rc=$rc: $(ls $out | wc -l) files, $(grep -c 'Segmentation' $log.out) segfaults"
}
run /tmp/city/nt_dev23.exe /tmp/city/dev22_shots /tmp/city/dev22_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev22_shot_args.txt)
until grep -q "cq23 done" /tmp/city/cq23.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev23b.exe ] || { st "no dev23b exe"; exit 1; }
run /tmp/city/nt_dev23b.exe /tmp/city/dev22_before_shots /tmp/city/dev22_before_shots_log.txt --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev22_shot_args.txt)
st "start dev22 regression"
for try in 1 2; do
  grep -q "SUMMARY: " /tmp/city/reg_dev22_log.txt 2>/dev/null && break
  mkdir -p /tmp/city/reg_dev22
  gate; TIMEOUT=9000 EXE=/tmp/city/nt_dev23.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --quality 0 --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev22\' > /tmp/city/reg_dev22.out 2>&1
  cp $L /tmp/city/reg_dev22_log.txt; rm -rf /tmp/city/reg_dev22
  st "dev22 regression (try $try): $(grep -c 'PASSED at' /tmp/city/reg_dev22_log.txt) passed, $(grep -c 'Killed' /tmp/city/reg_dev22.out) killed, $(grep -c 'Segmentation' /tmp/city/reg_dev22.out) segfaults; $(grep -o 'SUMMARY: .*' /tmp/city/reg_dev22_log.txt)"
done
st "start tour 2-5 (dev22)"
gate; TIMEOUT=6000 EXE=/tmp/city/nt_dev23.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev22\' > /tmp/city/tour_dev22.out 2>&1
cp $L /tmp/city/tour_dev22_log.txt
st "jobs23 done"
