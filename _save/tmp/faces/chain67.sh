#!/bin/sh
# batch 11 proof (trimmed by the coordinator: no perf; 16 shots): after the O2 build of b11g, mouths of Mari, Dex, 17,
# 27, 57 (0.5 m day, 1 m night) and ears of the six standard people (0.5 m day, profile) with nt_b10_o2.exe (before)
# and nt_b11_o2.exe (after)
echo $$ > /tmp/faces/chain67.pid
cd /tmp/faces
while [ -e /tmp/faces/c_build_b11.pid ]; do sleep 30; done
grep -q "build exit 0" /tmp/faces/build_b11_o2.log || { echo "b11 build failed"; rm -f /tmp/faces/chain67.pid; exit 1; }
V="--viewer characters --protagonists --clip 0 --settle 14 --viewerlamp"
for tag in b10 b11; do
  EXE=/tmp/faces/nt_${tag}_o2.exe TIMEOUT=3600 sh /tmp/faces/shoot2.sh fb11_$tag $V $(cat fb11_trim.args) > /tmp/faces/shoot_fb11_$tag.out 2>&1
  echo "fb11_$tag done $(date +%T)"
done
echo "chain67 exit $(date +%T)"
rm -f /tmp/faces/chain67.pid
