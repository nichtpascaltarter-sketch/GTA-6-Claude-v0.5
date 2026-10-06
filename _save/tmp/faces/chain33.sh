#!/bin/sh
# batch 6 candidate (dev11 = main d3c2c3f + batch 5 + lateral orbital rims + lower lid margin normals): QUICK build,
# then the 30 standard portraits at 960x540 (compare with /tmp/faces/b5)
cd /tmp/faces/dev11 && QUICK=1 OUT=/tmp/faces/nt_d11.exe sh build.sh > /tmp/faces/build_d11.log 2>&1; echo "exit $?" >> /tmp/faces/build_d11.log
grep -q "^exit 0" /tmp/faces/build_d11.log || exit 1
cd /tmp/faces
EXE=/tmp/faces/nt_d11.exe TIMEOUT=3600 /tmp/faces/shoot2.sh d11 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat final_day.args) $(cat final_night.args) > /tmp/faces/shoot_d11.out 2>&1
echo done > /tmp/faces/chain33.done
