#!/bin/sh
# perf A/B for batch 7 (the first b7 perf ran at twice the usual cpu ms in ai, veh and peds alike: machine load): an O2
# build of main b907098, then (after the buzz test's games) perf of main and of batch 7 back to back, twice
cd /tmp/faces
while ! grep -q "^exit" /tmp/faces/build_d14.log 2>/dev/null; do sleep 20; done
cd /tmp/faces/m907 && OUT=/tmp/faces/nt_m907_o2.exe sh build.sh > /tmp/faces/build_m907_o2.log 2>&1; echo "exit $?" >> /tmp/faces/build_m907_o2.log
grep -q "^exit 0" /tmp/faces/build_m907_o2.log || { echo "build failed" > /tmp/faces/chain46.done; exit 1; }
cd /tmp/faces
while [ ! -f /tmp/faces/chain45.done ]; do sleep 20; done
for i in 1 2; do
  /tmp/faces/perf.sh /tmp/faces/nt_m907_o2.exe m907_$i > /tmp/faces/perf_m907_$i.out 2>&1
  /tmp/faces/perf.sh /tmp/faces/nt_b7_o2.exe b7_$i > /tmp/faces/perf_b7_$i.out 2>&1
done
echo done > /tmp/faces/chain46.done
