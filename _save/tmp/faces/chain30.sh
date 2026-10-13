#!/bin/sh
# batch 5 proof: O2 build of b5tree (main 8fb1e66 + batch 4 + batch 5's face.cpp), 30 portraits, crowd perf
cd /tmp/faces/b5tree && OUT=/tmp/faces/nt_b5_o2.exe sh build.sh > /tmp/faces/build_b5_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_b5_o2.log
grep -q "^exit 0" /tmp/faces/build_b5_o2.log || exit 1
cd /tmp/faces
EXE=/tmp/faces/nt_b5_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b5 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat final_day.args) $(cat final_night.args) > /tmp/faces/shoot_b5.out 2>&1
/tmp/faces/perf.sh /tmp/faces/nt_b5_o2.exe b5 > /tmp/faces/perf_b5.out 2>&1
