#!/bin/sh
# batch 9 proof (eye region): main 67e6159 O2 vs batch 9 O2, 48 shots each; then crowd perf A/B twice
echo $$ > /tmp/faces/chain58.pid
cd /tmp/faces
EXE=/tmp/faces/nt_m46_o2.exe TIMEOUT=5400 /tmp/faces/shoot2.sh fb9_m46 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat fb9_all.args) > /tmp/faces/shoot_fb9_m46.out 2>&1
echo "fb9_m46 done $(date +%T)"
EXE=/tmp/faces/nt_b9_o2.exe TIMEOUT=5400 /tmp/faces/shoot2.sh fb9_b9 --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat fb9_all.args) > /tmp/faces/shoot_fb9_b9.out 2>&1
echo "fb9_b9 done $(date +%T)"
for i in 1 2; do
  /tmp/faces/perf.sh /tmp/faces/nt_m46_o2.exe m46_$i > /tmp/faces/perf_m46_$i.out 2>&1; echo "perf m46_$i done $(date +%T)"
  /tmp/faces/perf.sh /tmp/faces/nt_b9_o2.exe b9_$i > /tmp/faces/perf_b9_$i.out 2>&1; echo "perf b9_$i done $(date +%T)"
done
echo "chain58 exit $(date +%T)"
rm -f /tmp/faces/chain58.pid
