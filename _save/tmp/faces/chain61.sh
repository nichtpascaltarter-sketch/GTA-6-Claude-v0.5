#!/bin/sh
# lips variants on the dev22 QUICK exe (HEAD 0d67933 + knobs FACES_LIP=ageFade,upperDark,borderSoft(xage),cornerDark and
# FACES_LIP2=redK,blueK): Mari, Dex, 7, 17, 38, 20 at 0.5/1 m day + 1 m night
echo $$ > /tmp/faces/chain61.pid
cd /tmp/faces
while [ -e /tmp/faces/c_build_d22.pid ]; do sleep 30; done
grep -q "build exit 0" /tmp/faces/build_d22.log || { echo "d22 build failed"; rm -f /tmp/faces/chain61.pid; exit 1; }
strings /tmp/faces/nt_d22.exe | grep -q "^FACES_LIP2$" || { echo "d22 has no FACES_LIP2"; rm -f /tmp/faces/chain61.pid; exit 1; }
run() { tag=$1; shift; env "$@" EXE=/tmp/faces/nt_d22.exe TIMEOUT=3600 /tmp/faces/shoot2.sh pv_$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat lipv.args) > /tmp/faces/shoot_pv_$tag.out 2>&1; echo "pv_$tag done $(date +%T)"; }
run P0 X=1
run P1 FACES_LIP=0.35,0,0.4,0.25 FACES_LIP2=0.96,1.22
run P2 FACES_LIP=0.35,0,0.4,0.25 FACES_LIP2=0.93,1.32
echo "chain61 exit $(date +%T)"
rm -f /tmp/faces/chain61.pid
