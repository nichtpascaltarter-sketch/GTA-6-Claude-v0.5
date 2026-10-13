#!/bin/sh
# upper-lid variants on the dev17 QUICK exe (FACES_LID=zScale,fwdScale,fill,blend): 6 people at 0.5/1 m day + 1 m night
echo $$ > /tmp/faces/chain55.pid
cd /tmp/faces
while [ -e /tmp/faces/chain54.pid ]; do sleep 30; done
run() { tag=$1; shift; env "$@" EXE=/tmp/faces/nt_d17.exe TIMEOUT=3000 /tmp/faces/shoot2.sh lv_$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat lidv.args) > /tmp/faces/shoot_lv_$tag.out 2>&1; echo "lv_$tag done $(date +%T)"; }
run L0 FACES_LID=0.8,1,0,0.008
run L1 FACES_LID=0.8,1,1,0.008
run L2 FACES_LID=0.8,1,1.5,0.008
echo "chain55 exit $(date +%T)"
rm -f /tmp/faces/chain55.pid
