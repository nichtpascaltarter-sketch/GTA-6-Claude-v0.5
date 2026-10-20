#!/bin/sh
# batch 11 (ear canal + fair-skin lips) proof: after chain65 (lip variants) and the O2 build of b11g, 47 shots each with
# nt_b10_o2.exe (before) and nt_b11_o2.exe (after), then perf b10/b11 alternately twice
echo $$ > /tmp/faces/chain66.pid
cd /tmp/faces
while [ -e /tmp/faces/chain65.pid ]; do sleep 30; done
while [ -e /tmp/faces/c_build_b11.pid ]; do sleep 30; done
grep -q "build exit 0" /tmp/faces/build_b11_o2.log || { echo "b11 build failed"; rm -f /tmp/faces/chain66.pid; exit 1; }
V="--viewer characters --protagonists --clip 0 --settle 14 --viewerlamp"
for tag in b10 b11; do
  EXE=/tmp/faces/nt_${tag}_o2.exe TIMEOUT=5400 sh /tmp/faces/shoot2.sh fb11_$tag $V $(cat fb11_all.args) > /tmp/faces/shoot_fb11_$tag.out 2>&1
  echo "fb11_$tag done $(date +%T)"
done
for i in 1 2; do
  sh /tmp/faces/perf.sh /tmp/faces/nt_b10_o2.exe b10x_$i > /tmp/faces/perf_b10x_$i.out 2>&1; echo "perf b10x_$i done $(date +%T)"
  sh /tmp/faces/perf.sh /tmp/faces/nt_b11_o2.exe b11_$i > /tmp/faces/perf_b11_$i.out 2>&1; echo "perf b11_$i done $(date +%T)"
done
echo "chain66 exit $(date +%T)"
rm -f /tmp/faces/chain66.pid
