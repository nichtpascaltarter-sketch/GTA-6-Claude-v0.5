#!/bin/sh
# batch 5 brow test (relaunched after the container restart): the dev9 QUICK build on the brow shot list
cd /tmp/faces
EXE=/tmp/faces/nt_d9.exe TIMEOUT=2400 /tmp/faces/shoot2.sh d9 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat brow.args) > /tmp/faces/shoot_d9.out 2>&1
