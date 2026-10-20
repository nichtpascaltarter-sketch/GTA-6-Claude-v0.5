#!/bin/sh
# batch 2: the shipped files' O2 build, then (in parallel) its perf run (also the Wine check) and the montage build
# (the flip off, for main's shaders), then the montage shots. One build and one game of mine at a time.
cd /tmp/faces/b2tree && OUT=/tmp/faces/nt_b2_o2.exe sh build.sh > /tmp/faces/build_b2_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b2_o2.log
[ -f /tmp/faces/nt_b2_o2.exe ] || exit 1
/tmp/faces/perf.sh /tmp/faces/nt_b2_o2.exe b2 > /tmp/faces/perf_b2.out 2>&1 &
PERF=$!
cd /tmp/faces/b2vis && QUICK=1 OUT=/tmp/faces/nt_b2vis.exe sh build.sh > /tmp/faces/build_b2vis.log 2>&1; echo "exit $?" >> /tmp/faces/build_b2vis.log
wait $PERF
[ -f /tmp/faces/nt_b2vis.exe ] || exit 1
cd /tmp/faces && ARGS="$(cat final_day.args) $(cat final_night.args)"
EXE=/tmp/faces/nt_b2vis.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b2v --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b2v.out 2>&1
