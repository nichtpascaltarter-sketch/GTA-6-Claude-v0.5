#!/bin/sh
# brow / lash variants, round 2 (k4's brows read faint at 1 m: keep the tint): FACES_BROWK=dens,tint,lash on nt_d13.exe
cd /tmp/faces
for v in "k6:0.55,0.85,0.75" "k7:0.45,1.0,0.75"; do
  tag=${v%%:*}; spec=${v#*:}
  FACES_BROWK=$spec EXE=/tmp/faces/nt_d13.exe TIMEOUT=2400 /tmp/faces/shoot2.sh d13$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat brow2.args) > /tmp/faces/shoot_d13$tag.out 2>&1
done
echo done > /tmp/faces/chain41.done
