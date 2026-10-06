#!/bin/sh
# batch 3 (final files, b3tree = main 3319337 + 4 files): O2 build, then after the base6 shots the 30 portraits, then
# the crowd perf run. One build and one game of mine at a time.
cd /tmp/faces/b3tree && OUT=/tmp/faces/nt_b3_o2.exe sh build.sh > /tmp/faces/build_b3_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b3_o2.log
[ -f /tmp/faces/nt_b3_o2.exe ] || exit 1
while ! grep -q "exit" /tmp/faces/shoot_base6.out 2>/dev/null; do sleep 10; done
cd /tmp/faces && ARGS="$(cat final_day.args) $(cat final_night.args)"
EXE=/tmp/faces/nt_b3_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b3 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b3.out 2>&1
/tmp/faces/perf.sh /tmp/faces/nt_b3_o2.exe b3 > /tmp/faces/perf_b3.out 2>&1
