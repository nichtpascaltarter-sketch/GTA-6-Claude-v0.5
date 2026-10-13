#!/bin/sh
# the montage "before" for batch 3: main 3319337 (QUICK build), shot after the batch-3 dev shots finish
cd /tmp/faces/base6_tree && QUICK=1 OUT=/tmp/faces/nt_base6.exe sh build.sh > /tmp/faces/build_base6.log 2>&1; echo "exit $?" >> /tmp/faces/build_base6.log
[ -f /tmp/faces/nt_base6.exe ] || exit 1
while ! grep -q "exit" /tmp/faces/shoot_d4b.out 2>/dev/null; do sleep 10; done
cd /tmp/faces && ARGS="$(cat final_day.args) $(cat final_night.args)"
EXE=/tmp/faces/nt_base6.exe TIMEOUT=3600 /tmp/faces/shoot2.sh base6 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_base6.out 2>&1
