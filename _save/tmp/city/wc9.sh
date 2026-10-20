#!/bin/bash
# worldcheck on dev9 (batches 5-7 on 6afc8b2) and dev10 (+ batch 8), after the citylab perf job
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/wc9.status; }
until grep -q "done\|failed" /tmp/city/clab10p.status 2>/dev/null; do sleep 15; done
for d in dev9 dev10; do
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
  cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/$d worldcheck.cpp -o wc_$d -lpthread > /tmp/city/wc_${d}_build.log 2>&1
  rc=$?; st "$d build rc=$rc"
  [ $rc -eq 0 ] || continue
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
  cd /home/user/GTA-6-Claude-v0.5 && timeout 3000 nice -n 5 $S/wc/wc_$d 12 > $S/wc/$d.txt 2>&1
  echo "exit $?" >> $S/wc/$d.txt
  st "$d wc done: $(grep -c 'worldcheck: passed' $S/wc/$d.txt) passed"
done
