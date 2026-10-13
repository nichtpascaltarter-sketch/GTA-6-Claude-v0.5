#!/bin/sh
# buzz-cut variants on the dev15 QUICK exe (knobs), people 6, 27, 35 at 0.5/1 m day + night
cd /tmp/faces
until [ -s /tmp/faces/nt_d15.exe ] && grep -q "build exit 0" /tmp/faces/build_d15.log; do sleep 30; done
run() { tag=$1; shift; env "$@" EXE=/tmp/faces/nt_d15.exe TIMEOUT=2400 /tmp/faces/shoot2.sh bv_$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat buzzv.args) > /tmp/faces/shoot_bv_$tag.out 2>&1; echo "bv_$tag done $(date +%T)"; }
run v1 FACES_BUZZ3=0.5,0,0.45,0.9 FACES_BUZZ4=5,3,4,0.6
run v2 FACES_BUZZ3=0.5,0.2,0.45,0.9 FACES_BUZZ4=5,3,4,0.6
run v3 FACES_BUZZ=0.005,0.008,0.009,0.009,0.6,0.4 FACES_BUZZ3=0.5,0,0.45,0.9 FACES_BUZZ4=5,3,4,0.6
echo "chain50 exit $(date +%T)"
