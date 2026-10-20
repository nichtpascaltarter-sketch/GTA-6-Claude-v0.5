#!/bin/bash
# batches 17 + 18 (world side) + 19: before views on nt_dev24 (HEAD): the far Solaris views at night with Reduce Flashing,
# the four MAT_PLASTIC views by day; after on nt_dev26: the night views (Reduce Flashing) and the day views (bags); after
# on nt_dev27 (cq29.sh): the four MAT_PLASTIC views; then the story regression (nt_dev27, quality 0, a retry if the
# out-of-memory killer takes it) and tours 2-5 on nt_dev27 and nt_dev24. One game at a time, behind the memory gate.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
SET=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/settings.ini
trap 'rm -f "$SET"' EXIT
st() { echo "$1 $(date)" >> /tmp/city/jobs30.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
shots() {   # exe, args file, out dir, reduce flashing 0/1
  mkdir -p /tmp/city/$3
  if [ "$4" = 1 ]; then printf 'reduce_flashing = 1\n' > "$SET"; else rm -f "$SET"; fi
  gate 3300; TIMEOUT=3600 EXE=$1 WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --width 960 --height 540 --settle 8 --gfxstats $(cat $2) --shotdir "Z:\\tmp\\city\\$3\\" > /tmp/city/$3_log.txt.out 2>&1
  rc=$?; rm -f "$SET"; cp $L /tmp/city/$3_log.txt
  st "$3 rc=$rc: $(ls /tmp/city/$3 | wc -l) files, $(grep -c 'Segmentation' /tmp/city/$3_log.txt.out) segfaults"
}
st "jobs32 start"
shots /tmp/city/nt_dev24.exe /tmp/city/dev26_night_before_args.txt dev25_before_night 1
shots /tmp/city/nt_dev24.exe /tmp/city/dev27_day_args.txt dev27_before_day 0
shots /tmp/city/nt_dev26.exe /tmp/city/dev26_night_after_args.txt dev26_after_night 1
shots /tmp/city/nt_dev26.exe /tmp/city/dev25_day_args.txt dev26_after_day 0
until grep -q "^dev27 exe ready\|^no dev27 exe\|^dev27 syntax errors" /tmp/city/cq26.status 2>/dev/null; do sleep 30; done
[ -f /tmp/city/nt_dev27.exe ] || { st "no nt_dev27.exe, stopped"; exit 1; }
shots /tmp/city/nt_dev27.exe /tmp/city/dev27_day_args.txt dev27_after_day 0
st "start dev27 regression"
for try in 1 2; do
  grep -q "SUMMARY: " /tmp/city/reg_dev27_log.txt 2>/dev/null && break
  mkdir -p /tmp/city/reg_dev27
  gate 3500; TIMEOUT=9000 EXE=/tmp/city/nt_dev27.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --quality 0 --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_dev27\' > /tmp/city/reg_dev27.out 2>&1
  cp $L /tmp/city/reg_dev27_log.txt; rm -rf /tmp/city/reg_dev27
  st "dev27 regression (try $try): $(grep -c 'PASSED at' /tmp/city/reg_dev27_log.txt) passed, $(grep -c 'Killed' /tmp/city/reg_dev27.out) killed, $(grep -c 'Segmentation' /tmp/city/reg_dev27.out) segfaults; $(grep -o 'SUMMARY: .*' /tmp/city/reg_dev27_log.txt)"
done
for T in dev27 dev24; do
  st "start tour 2-5 ($T)"
  mkdir -p /tmp/city/tour_$T
  gate 3300; TIMEOUT=6000 EXE=/tmp/city/nt_$T.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 2 --tourcount 4 --gfxstats --width 960 --height 540 --shotdir "Z:\\tmp\\city\\tour_$T\\" > /tmp/city/tour_$T.out 2>&1
  cp $L /tmp/city/tour_${T}_log.txt
  st "tour $T: $(grep -c 'autoplay tour done' /tmp/city/tour_${T}_log.txt) done, $(grep -c 'Segmentation\|Killed' /tmp/city/tour_$T.out) segfaults/kills"
done
st "jobs32 done"
