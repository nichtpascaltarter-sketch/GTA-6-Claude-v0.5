#!/bin/sh
# more buzz-cut variants (after chain50): flow jitter (scattered highlights)
cd /tmp/faces
until grep -q "chain50 exit" /tmp/faces/chain50.out 2>/dev/null; do sleep 30; done
run() { tag=$1; shift; env "$@" EXE=/tmp/faces/nt_d15.exe TIMEOUT=2400 /tmp/faces/shoot2.sh bv_$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat buzzv.args) > /tmp/faces/shoot_bv_$tag.out 2>&1; echo "bv_$tag done $(date +%T)"; }
run v4 FACES_BUZZ3=0.5,0,0.45,0.9 FACES_BUZZ4=4,1,4,0.6 FACES_BUZZ5=0.8
echo "chain51 exit $(date +%T)"
