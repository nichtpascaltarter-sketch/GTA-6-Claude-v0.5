#!/bin/sh
# batch 8 "before": main 61a3b7b O2 (nt_m61_o2.exe), the 66-shot proof list (30 portraits, buzz 6/27/35/67, hats 5/15/19/33)
echo $$ > /tmp/faces/chain53.pid
cd /tmp/faces
while [ -e /tmp/faces/chain52.pid ]; do sleep 30; done
EXE=/tmp/faces/nt_m61_o2.exe TIMEOUT=5400 /tmp/faces/shoot2.sh fb_m61 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat fb_all.args) > /tmp/faces/shoot_fb_m61.out 2>&1
echo "fb_m61 done $(date +%T)"
echo "chain53 exit $(date +%T)"
rm -f /tmp/faces/chain53.pid
