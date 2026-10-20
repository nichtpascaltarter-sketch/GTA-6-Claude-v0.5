#!/bin/sh
# eye-region variants on the dev19 QUICK exe (c1af87a + batch 8 + knobs; FACES_LID fill + FACES_NAS shallower nasion):
# Mari, Dex, 7, 38 at 0.5/1 m day + 1 m night
echo $$ > /tmp/faces/chain56.pid
cd /tmp/faces
while [ -e /tmp/faces/c_build_d19.pid ]; do sleep 30; done
grep -q "build exit 0" /tmp/faces/build_d19.log || { echo "d19 build failed"; rm -f /tmp/faces/chain56.pid; exit 1; }
run() { tag=$1; shift; env "$@" EXE=/tmp/faces/nt_d19.exe TIMEOUT=3000 /tmp/faces/shoot2.sh nv_$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat nasv.args) > /tmp/faces/shoot_nv_$tag.out 2>&1; echo "nv_$tag done $(date +%T)"; }
run N0 FACES_LID=0.8,1,0,0.008
run N2 FACES_LID=0.8,1,1.5,0.008 FACES_NAS=4,0
run N3 FACES_LID=0.8,1,1.5,0.008 FACES_NAS=7,0
echo "chain56 exit $(date +%T)"
rm -f /tmp/faces/chain56.pid
