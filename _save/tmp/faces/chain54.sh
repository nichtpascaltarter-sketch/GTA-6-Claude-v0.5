#!/bin/sh
# batch 8 proof, after the "before" shots (chain53): b8 O2 66 shots; frame cost (synctimers) m61 vs b8; crowd perf A/B twice
echo $$ > /tmp/faces/chain54.pid
cd /tmp/faces
while [ -e /tmp/faces/chain53.pid ] || [ -e /tmp/faces/c_build_b8.pid ]; do sleep 30; done
grep -q "build exit 0" /tmp/faces/build_b8_o2.log || { echo "b8 build failed"; rm -f /tmp/faces/chain54.pid; exit 1; }
EXE=/tmp/faces/nt_b8_o2.exe TIMEOUT=5400 /tmp/faces/shoot2.sh fb_b8 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat fb_all.args) > /tmp/faces/shoot_fb_b8.out 2>&1
echo "fb_b8 done $(date +%T)"
/tmp/faces/perf_gpu.sh /tmp/faces/nt_m61_o2.exe m61 > /tmp/faces/gpu_m61.txt 2>&1; echo "gpu_m61 done $(date +%T)"
/tmp/faces/perf_gpu.sh /tmp/faces/nt_b8_o2.exe b8 > /tmp/faces/gpu_b8.txt 2>&1; echo "gpu_b8 done $(date +%T)"
for i in 1 2; do
  /tmp/faces/perf.sh /tmp/faces/nt_m61_o2.exe m61_$i > /tmp/faces/perf_m61_$i.out 2>&1; echo "perf m61_$i done $(date +%T)"
  /tmp/faces/perf.sh /tmp/faces/nt_b8_o2.exe b8_$i > /tmp/faces/perf_b8_$i.out 2>&1; echo "perf b8_$i done $(date +%T)"
done
echo "chain54 exit $(date +%T)"
rm -f /tmp/faces/chain54.pid
