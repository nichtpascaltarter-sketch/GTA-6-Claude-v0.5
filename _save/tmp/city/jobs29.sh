#!/bin/bash
# tour stop 5 (beach condos) again on both builds: the first pair differed by +118 draws (583 -> 701), with the
# simulation diverged (a fender bender and a hug in one run only). After first, then before.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs23.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
for T in dev23c dev23b dev23c dev23b; do
  n=$(ls /tmp/city/tour5_${T}_*_log.txt 2>/dev/null | wc -l); n=$((n + 1))
  mkdir -p /tmp/city/tour5_$T
  gate 3300; TIMEOUT=3600 EXE=/tmp/city/nt_$T.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 5 --tourcount 1 --gfxstats --width 960 --height 540 --shotdir "Z:\\tmp\\city\\tour5_$T\\" > /tmp/city/tour5_${T}_$n.out 2>&1
  cp $L /tmp/city/tour5_${T}_${n}_log.txt
  st "tour5 $T run $n: $(grep -c 'autoplay tour done' /tmp/city/tour5_${T}_${n}_log.txt) done, $(grep -c 'Segmentation\|Killed' /tmp/city/tour5_${T}_$n.out) segfaults/kills"
done
st "jobs29 done"
