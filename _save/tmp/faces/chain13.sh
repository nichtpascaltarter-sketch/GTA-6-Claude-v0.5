#!/bin/sh
# batch 2 re-check on main 06580c3: the O2 build, then a short Wine run (8 portraits, day and night)
cd /tmp/faces/b2t6 && OUT=/tmp/faces/nt_b2d_o2.exe sh build.sh > /tmp/faces/build_b2d_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b2d_o2.log
[ -f /tmp/faces/nt_b2d_o2.exe ] || exit 1
cd /tmp/faces && ARGS="$(cat flip_day.args) $(cat flip_night.args)"
EXE=/tmp/faces/nt_b2d_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b2d --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b2d.out 2>&1
