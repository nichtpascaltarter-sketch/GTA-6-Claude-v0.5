#!/bin/sh
# lips variants on the dev21 QUICK exe (batch 9 + FACES_LIP=ageDesat,upperDark,borderSoft,cornerDark), after chain59
echo $$ > /tmp/faces/chain60.pid
cd /tmp/faces
while [ -e /tmp/faces/chain59.pid ]; do sleep 30; done
run() { tag=$1; shift; env "$@" EXE=/tmp/faces/nt_d21.exe TIMEOUT=3600 /tmp/faces/shoot2.sh pv_$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat lipv.args) > /tmp/faces/shoot_pv_$tag.out 2>&1; echo "pv_$tag done $(date +%T)"; }
run P0 FACES_LIP=0,0,0,0.18
run P1 FACES_LIP=0.5,0.08,0.4,0.25
run P2 FACES_LIP=0.7,0.12,0.6,0.3
echo "chain60 exit $(date +%T)"
rm -f /tmp/faces/chain60.pid
