#!/bin/sh
# batch 6 proof with the O2 build of b6tree (main cda29a1 + face.cpp + anim_test.cpp): after the d11 shots, the 30
# standard portraits -> /tmp/faces/b6, then the crowd perf (tour stop 7) -> perf_b6
cd /tmp/faces
while [ ! -f /tmp/faces/chain33.done ] && kill -0 28510 2>/dev/null; do sleep 15; done
EXE=/tmp/faces/nt_b6_o2.exe TIMEOUT=3600 /tmp/faces/shoot2.sh b6 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat final_day.args) $(cat final_night.args) > /tmp/faces/shoot_b6.out 2>&1
/tmp/faces/perf.sh /tmp/faces/nt_b6_o2.exe b6 > /tmp/faces/perf_b6.out 2>&1
echo done > /tmp/faces/chain35.done
