#!/bin/sh
# batch 7 proof: O2 build of b7tree (main b907098 + face.cpp, anim_test.cpp, preview.cpp), 30 portraits -> b7, perf
cd /tmp/faces/b7tree && OUT=/tmp/faces/nt_b7_o2.exe sh build.sh > /tmp/faces/build_b7_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b7_o2.log
grep -q "^exit 0" /tmp/faces/build_b7_o2.log || { echo "build failed" > /tmp/faces/chain42.done; exit 1; }
cd /tmp/faces
EXE=/tmp/faces/nt_b7_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b7 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat final_day.args) $(cat final_night.args) > /tmp/faces/shoot_b7.out 2>&1
/tmp/faces/perf.sh /tmp/faces/nt_b7_o2.exe b7 > /tmp/faces/perf_b7.out 2>&1
echo done > /tmp/faces/chain42.done
