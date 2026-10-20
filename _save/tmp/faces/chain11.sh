#!/bin/sh
# batch 2 (side-hair fix): the shipped files' O2 build, the montage build (flip off), then the montage shots
cd /tmp/faces/b2tree && OUT=/tmp/faces/nt_b2b_o2.exe sh build.sh > /tmp/faces/build_b2b_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b2b_o2.log
cd /tmp/faces/b2vis && QUICK=1 OUT=/tmp/faces/nt_b2bvis.exe sh build.sh > /tmp/faces/build_b2bvis.log 2>&1; echo "exit $?" >> /tmp/faces/build_b2bvis.log
[ -f /tmp/faces/nt_b2bvis.exe ] || exit 1
cd /tmp/faces && ARGS="$(cat final_day.args) $(cat final_night.args)"
EXE=/tmp/faces/nt_b2bvis.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b2w --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b2w.out 2>&1
