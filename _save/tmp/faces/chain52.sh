#!/bin/sh
# buzz-cut variants round 2 on the dev16 QUICK exe (knobs incl. segments/layers): people 6, 27, 35 at 0.5/1 m day + night
echo $$ > /tmp/faces/chain52.pid
cd /tmp/faces
run() { tag=$1; shift; env "$@" EXE=/tmp/faces/nt_d16.exe TIMEOUT=2400 /tmp/faces/shoot2.sh bv_$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat buzzv.args) > /tmp/faces/shoot_bv_$tag.out 2>&1; echo "bv_$tag done $(date +%T)"; }
run v4 FACES_BUZZ3=0.5,0,0.45,0.9 FACES_BUZZ4=4,1,4,0.6 FACES_BUZZ5=0.8
run v7 FACES_BUZZ=0.004,0.006,0.007,0.008,0.5,0.4,1,2 FACES_BUZZ3=0.5,0,0.45,0.9 FACES_BUZZ4=5,3,4,0.6 FACES_BUZZ5=0.8
echo "chain52 exit $(date +%T)"
rm -f /tmp/faces/chain52.pid
