#!/bin/bash
# footprint check before (dev14) and after (dev15): build each worldcheck, run with FP_NEAR (North Porto Sol shot)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/wcfp.status; }
export FP_NEAR=2618,4318,40 FP_SHOTS=1
for pair in "dev14 fp_b" "dev15 fp_a"; do
  set -- $pair; T=$1; TAG=$2
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 10; done
  st "wc build ($T $TAG)"
  cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/$T worldcheck.cpp -o wc_$TAG -lpthread > /tmp/city/wc_${TAG}_build.log 2>&1
  rc=$?; st "wc build ($T $TAG) rc=$rc"; [ $rc -eq 0 ] || continue
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
  cd /home/user/GTA-6-Claude-v0.5
  nice -n 5 $S/wc/wc_$TAG 20 > $S/wc/$TAG.txt 2>&1 &
  pid=$!; hwm=0
  while kill -0 $pid 2>/dev/null; do h=$(grep VmHWM /proc/$pid/status 2>/dev/null | grep -o '[0-9]*'); [ -n "$h" ] && hwm=$h; sleep 2; done
  wait $pid; rc=$?
  st "wc run ($T $TAG) exit $rc: $(grep -o '[0-9]* overlapping building pairs.*' $S/wc/$TAG.txt) (peak $((hwm / 1024)) MB)"
done
st "wcfp2 done"
