#!/bin/sh
# hair under hats, before / after batch 7 (both on snapshot 36's shaders): people 2, 4, 22, 23 at 0.5 and 1 m by day and
# 1 m at night; after chain42
cd /tmp/faces
while [ ! -f /tmp/faces/chain42.done ]; do sleep 20; done
EXE=/tmp/faces/nt_b6_o2.exe TIMEOUT=2400 /tmp/faces/shoot2.sh hats_b6s36 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp --shaderdir 'Z:\tmp\faces\shd_s36\' $(cat hats.args) > /tmp/faces/shoot_hats_b6s36.out 2>&1
EXE=/tmp/faces/nt_b7_o2.exe TIMEOUT=2400 /tmp/faces/shoot2.sh hats_b7 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat hats.args) > /tmp/faces/shoot_hats_b7.out 2>&1
echo done > /tmp/faces/chain43.done
