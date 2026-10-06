#!/bin/sh
# batch 1e (the beard shell builds up gradually next to the lips): build, men's shots; then the next dev build and its
# close-ups. One build and one game of mine at a time.
cd /tmp/faces/b1tree && OUT=/tmp/faces/nt_b1e_o2.exe sh build.sh > /tmp/faces/build_b1e_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b1e_o2.log
cd /tmp/faces && ARGS="$(cat men_day.args) $(cat men_night.args)"
EXE=/tmp/faces/nt_b1e_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b1e --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b1e.out 2>&1 &
SHOOT=$!
rm -rf /tmp/faces/dev4tree && cp -r /tmp/faces/base_tree /tmp/faces/dev4tree
for f in face.cpp hair.cpp character.cpp meshutil.cpp bodymesh.cpp; do cp /tmp/faces/dev/src/anim/$f /tmp/faces/dev4tree/src/anim/$f; done
cd /tmp/faces/dev4tree && QUICK=1 OUT=/tmp/faces/nt_dev4.exe sh build.sh > /tmp/faces/build_dev4.log 2>&1; echo "exit $?" >> /tmp/faces/build_dev4.log
wait $SHOOT
cd /tmp/faces && ARGS="$(cat iter2_day.args) $(cat iter2_night.args)"
EXE=/tmp/faces/nt_dev4.exe TIMEOUT=3600 /tmp/faces/shoot2.sh dev4 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_dev4.out 2>&1
