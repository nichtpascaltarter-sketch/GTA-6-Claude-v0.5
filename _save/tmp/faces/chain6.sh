#!/bin/sh
# batch-2 dev build after the batch-1d build, then (after the batch-1d shots) the dev close-ups
while ! grep -q "exit" /tmp/faces/build_b1d_o2.log 2>/dev/null; do sleep 10; done
rm -rf /tmp/faces/dev3tree && cp -r /tmp/faces/base_tree /tmp/faces/dev3tree
for f in face.cpp hair.cpp character.cpp meshutil.cpp bodymesh.cpp; do cp /tmp/faces/dev/src/anim/$f /tmp/faces/dev3tree/src/anim/$f; done
cd /tmp/faces/dev3tree && QUICK=1 OUT=/tmp/faces/nt_dev3.exe sh build.sh > /tmp/faces/build_dev3.log 2>&1; echo "exit $?" >> /tmp/faces/build_dev3.log
while ! grep -q "exit" /tmp/faces/shoot_b1d.out 2>/dev/null; do sleep 10; done
cd /tmp/faces && ARGS="$(cat iter2_day.args) $(cat iter2_night.args)"
EXE=/tmp/faces/nt_dev3.exe TIMEOUT=3600 /tmp/faces/shoot2.sh dev3 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_dev3.out 2>&1
