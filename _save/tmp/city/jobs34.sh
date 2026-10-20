#!/bin/bash
# tour stop 5 (beach condos) alone on both builds: the 4-stop tours split there (nt_dev24 688/729 vs nt_dev27 584/605,
# peds 122 vs 118), the run-history split seen on 13 Oct. After first, then before. One game at a time, behind the gate;
# after tb4.sh (my build) is done.
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs30.status; }
cd /home/user/GTA-6-Claude-v0.5
gate() { while [ "$(sh tools/memfree.sh)" -lt $1 ]; do sleep 20; done; }
until grep -q "^tb4 done" /tmp/city/tb.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/tb4.sh" > /dev/null; do sleep 20; done
for T in dev27 dev24; do
  mkdir -p /tmp/city/tour5_$T
  gate 3300; TIMEOUT=3600 EXE=/tmp/city/nt_$T.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --autoplay tour --tourstart 5 --tourcount 1 --gfxstats --width 960 --height 540 --shotdir "Z:\\tmp\\city\\tour5_$T\\" > /tmp/city/tour5_$T.out 2>&1
  cp $L /tmp/city/tour5_${T}_log.txt
  st "tour5 $T: $(grep -c 'autoplay tour done' /tmp/city/tour5_${T}_log.txt) done, $(grep -c 'Segmentation\|Killed' /tmp/city/tour5_$T.out) segfaults/kills"
done
rm -rf /tmp/city/tour5_dev27 /tmp/city/tour5_dev24
st "jobs34 done"
