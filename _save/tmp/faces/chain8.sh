#!/bin/sh
# batch-2 dev build on the new base (HEAD + batch 1) and its close-ups; one build and one game of mine at a time
rm -rf /tmp/faces/dev5tree && cp -r /tmp/faces/base2_tree /tmp/faces/dev5tree
for f in face.cpp hair.cpp character.cpp meshutil.cpp bodymesh.cpp; do cp /tmp/faces/dev/src/anim/$f /tmp/faces/dev5tree/src/anim/$f; done
cd /tmp/faces/dev5tree && QUICK=1 OUT=/tmp/faces/nt_dev5.exe sh build.sh > /tmp/faces/build_dev5.log 2>&1; echo "exit $?" >> /tmp/faces/build_dev5.log
[ -f /tmp/faces/nt_dev5.exe ] || exit 1
cd /tmp/faces && ARGS="$(cat iter2_day.args) $(cat iter2_night.args)"
EXE=/tmp/faces/nt_dev5.exe TIMEOUT=3600 /tmp/faces/shoot2.sh dev5 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_dev5.out 2>&1
