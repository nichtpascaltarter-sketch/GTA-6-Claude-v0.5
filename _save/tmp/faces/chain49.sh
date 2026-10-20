#!/bin/sh
# quick look at the dev14 buzz-cut card prototype: same exe, cards off vs on (people 6, 27, 35; 0.5/1/4 m day + night)
cd /tmp/faces
EXE=/tmp/faces/nt_d14.exe TIMEOUT=2400 /tmp/faces/shoot2.sh buzz_off --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat buzz3.args) > /tmp/faces/shoot_buzz_off.out 2>&1
FACES_BUZZ=0.007,0.011,0.011,0.011,0.6,0.4 EXE=/tmp/faces/nt_d14.exe TIMEOUT=2400 /tmp/faces/shoot2.sh buzz_on --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat buzz3.args) > /tmp/faces/shoot_buzz_on.out 2>&1
echo "chain49 exit $(date +%T)"
