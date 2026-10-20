#!/bin/sh
# batch 10 (lips) proof, continued (main O2 nt_m0d_o2.exe built at 15:43): O2 build of b10tree, 40 shots each, perf A/B twice
echo $$ > /tmp/faces/chain63.pid
cd /tmp/faces
for t in b10tree:b10; do
  tree=${t%%:*}; tag=${t#*:}
  sh /tmp/faces/gate.sh 2600 > /tmp/faces/gate_build_$tag.log 2>&1
  (cd /tmp/faces/$tree && OUT=/tmp/faces/nt_${tag}_o2.exe sh build.sh > /tmp/faces/build_${tag}_o2.log 2>&1; echo "build exit $? $(date +%T)" >> /tmp/faces/build_${tag}_o2.log)
  echo "build $tag $(tail -1 /tmp/faces/build_${tag}_o2.log)"
done
grep -q "build exit 0" /tmp/faces/build_m0d_o2.log && grep -q "build exit 0" /tmp/faces/build_b10_o2.log || { echo "a build failed"; rm -f /tmp/faces/chain63.pid; exit 1; }
for tag in m0d b10; do
  EXE=/tmp/faces/nt_${tag}_o2.exe TIMEOUT=5400 /tmp/faces/shoot2.sh fb10_$tag --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat fb10_all.args) > /tmp/faces/shoot_fb10_$tag.out 2>&1
  echo "fb10_$tag done $(date +%T)"
done
for i in 1 2; do
  /tmp/faces/perf.sh /tmp/faces/nt_m0d_o2.exe m0d_$i > /tmp/faces/perf_m0d_$i.out 2>&1; echo "perf m0d_$i done $(date +%T)"
  /tmp/faces/perf.sh /tmp/faces/nt_b10_o2.exe b10_$i > /tmp/faces/perf_b10_$i.out 2>&1; echo "perf b10_$i done $(date +%T)"
done
echo "chain63 exit $(date +%T)"
rm -f /tmp/faces/chain63.pid
