#!/bin/sh
# batch 3: the "before" shots (06580c3 + batch 2, nt_b2d_o2.exe) while the dev build runs, then the dev shots.
# One game and one build of mine at a time.
cd /tmp/faces && ARGS="$(cat final_day.args) $(cat final_night.args)"
EXE=/tmp/faces/nt_b2d_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b2full --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b2full.out 2>&1 &
SHOOT=$!
cd /tmp/faces/dev4 && QUICK=1 OUT=/tmp/faces/nt_dev4.exe sh build.sh > /tmp/faces/build_dev4b.log 2>&1; echo "exit $?" >> /tmp/faces/build_dev4b.log
wait $SHOOT
[ -f /tmp/faces/nt_dev4.exe ] || exit 1
cd /tmp/faces && EXE=/tmp/faces/nt_dev4.exe TIMEOUT=3600 /tmp/faces/shoot2.sh d4 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_d4.out 2>&1
