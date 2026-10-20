#!/bin/bash
# batches 12 + 14 + 15, the rest of the tests (replaces jobs26.sh, stopped at 06:43 before its beacon game started):
# the beacon close views on nt_dev23b (before), then on nt_dev23c (after: dev23 + bag pile v2) once cq23c.sh has built
# it, both with Reduce Flashing on (settings.ini, removed after each run); the bag views again on nt_dev23c; the story
# regression (nt_dev23c, quality 0, a retry if the out-of-memory killer takes it); tour stops 2-5 on nt_dev23c.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
SET=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/settings.ini
trap 'rm -f "$SET"' EXIT
st() { echo "$1 $(date)" >> /tmp/city/jobs23.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
beacon() {
  mkdir -p /tmp/city/dev22_beacon_$1
  printf 'reduce_flashing = 1\n' > "$SET"
  gate 3300; TIMEOUT=1800 EXE=$2 WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev22_beacon_args.txt) --shotdir "Z:\\tmp\\city\\dev22_beacon_$1\\" > /tmp/city/dev22_beacon_$1_log.txt.out 2>&1
  rc=$?; rm -f "$SET"; cp $L /tmp/city/dev22_beacon_$1_log.txt
  st "dev22_beacon_$1_log.txt rc=$rc: $(ls /tmp/city/dev22_beacon_$1 | wc -l) files, $(grep -c 'Segmentation' /tmp/city/dev22_beacon_$1_log.txt.out) segfaults"
}
st "jobs27 start"
beacon before /tmp/city/nt_dev23b.exe
until grep -q "^exe ready\|^no exe\|^syntax errors" /tmp/city/cq23c.status 2>/dev/null; do sleep 30; done
if [ ! -f /tmp/city/nt_dev23c.exe ]; then st "jobs27: no nt_dev23c.exe, stopped"; exit 1; fi
beacon after /tmp/city/nt_dev23c.exe
mkdir -p /tmp/city/dev22c_bag_shots
gate 3300; TIMEOUT=2400 EXE=/tmp/city/nt_dev23c.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats $(cat /tmp/city/dev22c_bag_args.txt) --shotdir 'Z:\tmp\city\dev22c_bag_shots\' > /tmp/city/dev22c_bag_log.txt.out 2>&1
rc=$?; cp $L /tmp/city/dev22c_bag_log.txt
st "dev22c_bag_log.txt rc=$rc: $(ls /tmp/city/dev22c_bag_shots | wc -l) files, $(grep -c 'Segmentation' /tmp/city/dev22c_bag_log.txt.out) segfaults"
st "start dev22c regression"
for try in 1 2; do
  grep -q "SUMMARY: " /tmp/city/reg_dev22c_log.txt 2>/dev/null && break
  mkdir -p /tmp/city/reg_dev22c
  gate 3500; TIMEOUT=9000 EXE=/tmp/city/nt_dev23c.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --quality 0 --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev22c\' > /tmp/city/reg_dev22c.out 2>&1
  cp $L /tmp/city/reg_dev22c_log.txt; rm -rf /tmp/city/reg_dev22c
  st "dev22c regression (try $try): $(grep -c 'PASSED at' /tmp/city/reg_dev22c_log.txt) passed, $(grep -c 'Killed' /tmp/city/reg_dev22c.out) killed, $(grep -c 'Segmentation' /tmp/city/reg_dev22c.out) segfaults; $(grep -o 'SUMMARY: .*' /tmp/city/reg_dev22c_log.txt)"
done
st "start tour 2-5 (dev23c)"
mkdir -p /tmp/city/tour_dev22c
gate 3300; TIMEOUT=6000 EXE=/tmp/city/nt_dev23c.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir 'Z:\tmp\city\tour_dev22c\' > /tmp/city/tour_dev22c.out 2>&1
cp $L /tmp/city/tour_dev22c_log.txt
st "jobs23 done"
