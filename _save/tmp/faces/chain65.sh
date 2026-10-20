#!/bin/sh
# A: ear baseline (batch 10 exe; ears as on main): 30 shots ears2.args -> ear_b10/
# B: after the dev23 QUICK build: V0 (blue lift 1.22) + ear fix FACES_EAR=1 with lip and ear shots; lip variants V1 h28,
#    V2 h31, V3 L1.12 (lip shots only)
cd /tmp/faces
V="--viewer characters --protagonists --clip 0 --settle 14 --viewerlamp"
# (the dev23 build goes first: wait until its gate has passed and it has had 90 s to take its memory, so the game's
# memory gate sees it)
until grep -q "gate passed" /tmp/faces/gate_build_d23.log 2>/dev/null || [ ! -e /tmp/faces/c_build_d23.pid ]; do sleep 20; done
sleep 90
EXE=/tmp/faces/nt_b10_o2.exe TIMEOUT=3600 sh /tmp/faces/shoot2.sh ear_b10 $V $(cat ears2.args) > /tmp/faces/shoot_ear_b10.out 2>&1
echo "ear_b10 done $(date +%T)"
while [ -e /tmp/faces/c_build_d23.pid ]; do sleep 30; done
grep -q "build exit 0" /tmp/faces/build_d23.log || { echo "d23 build failed"; rm -f /tmp/faces/chain65.pid; exit 1; }
for k in FACES_EAR FACES_LIPB; do strings /tmp/faces/nt_d23.exe | grep -q -E "(^|[^A-Z_])$k\$" || { echo "d23 has no $k"; rm -f /tmp/faces/chain65.pid; exit 1; }; done
run() { tag=$1; args=$2; shift 2; env "$@" EXE=/tmp/faces/nt_d23.exe TIMEOUT=3600 sh /tmp/faces/shoot2.sh d23_$tag $V $(cat $args) > /tmp/faces/shoot_d23_$tag.out 2>&1; echo "d23_$tag done $(date +%T)"; }
cat lipb.args earfix.args > lipear.args
run V0 lipear.args FACES_EAR=1
run V1 lipb.args FACES_EAR=1 FACES_LIPB=h28
run V2 lipb.args FACES_EAR=1 FACES_LIPB=h31
run V3 lipb.args FACES_EAR=1 FACES_LIPB=L1.12
echo "chain65 exit $(date +%T)"
rm -f /tmp/faces/chain65.pid
