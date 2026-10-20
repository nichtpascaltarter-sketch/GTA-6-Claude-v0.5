#!/bin/sh
# batch-2 dev build after the batch-1c build, then (after chain3's games) the dev close-ups; one build and one game of
# mine at a time. Base: the same tree as the batch-1 comparisons (base_tree) with the dev anim files.
while ! grep -q "exit" /tmp/faces/build_b1c_o2.log 2>/dev/null; do sleep 10; done
rm -rf /tmp/faces/dev2tree && cp -r /tmp/faces/base_tree /tmp/faces/dev2tree
for f in face.cpp hair.cpp character.cpp meshutil.cpp bodymesh.cpp; do cp /tmp/faces/dev/src/anim/$f /tmp/faces/dev2tree/src/anim/$f; done
cd /tmp/faces/dev2tree && QUICK=1 OUT=/tmp/faces/nt_dev2.exe sh build.sh > /tmp/faces/build_dev2b.log 2>&1; echo "exit $?" >> /tmp/faces/build_dev2b.log
while ! grep -q "exit" /tmp/faces/perf_b1c.out 2>/dev/null; do sleep 10; done
cd /tmp/faces && ARGS="$(cat iter2_day.args) $(cat iter2_night.args)"
EXE=/tmp/faces/nt_dev2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh dev2 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_dev2.out 2>&1
