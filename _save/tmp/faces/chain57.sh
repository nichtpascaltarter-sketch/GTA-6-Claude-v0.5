#!/bin/sh
# eye-region variants on the dev20 QUICK exe (snapshot 46 67e6159 + knobs): lid fill + nasion + glabella fill
echo $$ > /tmp/faces/chain57.pid
cd /tmp/faces
while [ -e /tmp/faces/c_build_d20.pid ] || [ -e /tmp/faces/chain56.pid ]; do sleep 30; done
grep -q "build exit 0" /tmp/faces/build_d20.log || { echo "d20 build failed"; rm -f /tmp/faces/chain57.pid; exit 1; }
strings /tmp/faces/nt_d20.exe | grep -q FACES_GLA || { echo "d20 has no FACES_GLA"; rm -f /tmp/faces/chain57.pid; exit 1; }
run() { tag=$1; shift; env "$@" EXE=/tmp/faces/nt_d20.exe TIMEOUT=3600 /tmp/faces/shoot2.sh gv_$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat glav.args) > /tmp/faces/shoot_gv_$tag.out 2>&1; echo "gv_$tag done $(date +%T)"; }
run G0 FACES_LID=0.8,1,0,0.008
run G1 FACES_LID=0.8,1,1.5,0.008 FACES_NAS=7,0 FACES_GLA=1.5,3
run G2 FACES_LID=0.8,1,1.5,0.008 FACES_NAS=4,0 FACES_GLA=1,3
echo "chain57 exit $(date +%T)"
rm -f /tmp/faces/chain57.pid
