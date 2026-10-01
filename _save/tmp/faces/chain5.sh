#!/bin/sh
# batch 1d (beard cards thin out before the lips): build, then after chain3's perf run the men's shots; one build and
# one game of mine at a time
cd /tmp/faces/b1tree && OUT=/tmp/faces/nt_b1d_o2.exe sh build.sh > /tmp/faces/build_b1d_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b1d_o2.log
while ! grep -q "exit" /tmp/faces/perf_b1c.out 2>/dev/null; do sleep 10; done
cd /tmp/faces && ARGS="$(cat men_day.args) $(cat men_night.args)"
EXE=/tmp/faces/nt_b1d_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b1d --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $ARGS > /tmp/faces/shoot_b1d.out 2>&1
