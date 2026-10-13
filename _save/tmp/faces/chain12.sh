#!/bin/sh
# batch 2 on main 4b6357f (the renderer's character shading + the eye flip): the shipped files' O2 build, then the
# base build (4b6357f, QUICK) while the batch-2 shots run, then the base shots. One build and one game of mine at a time.
cd /tmp/faces/b2t5 && OUT=/tmp/faces/nt_b2c_o2.exe sh build.sh > /tmp/faces/build_b2c_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b2c_o2.log
[ -f /tmp/faces/nt_b2c_o2.exe ] || exit 1
cd /tmp/faces && ARGS="$(cat final_day.args) $(cat final_night.args)"
EXE=/tmp/faces/nt_b2c_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b2c --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b2c.out 2>&1 &
SHOOT=$!
cd /tmp/faces/base5_tree && QUICK=1 OUT=/tmp/faces/nt_base5.exe sh build.sh > /tmp/faces/build_base5.log 2>&1; echo "exit $?" >> /tmp/faces/build_base5.log
wait $SHOOT
[ -f /tmp/faces/nt_base5.exe ] || exit 1
cd /tmp/faces && EXE=/tmp/faces/nt_base5.exe TIMEOUT=3600 /tmp/faces/shoot2.sh base5 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_base5.out 2>&1
