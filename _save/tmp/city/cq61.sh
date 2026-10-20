#!/bin/bash
# Road-overlap fix: tools/worldcheck on base (the shared tree = HEAD bcb8a71 + AI's traffic_sim, roads.cpp 4569edfb) and on
# the fix tree ($S/rf/t2), then the two outputs side by side (timings masked).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq61 start"
for V in base fix; do
  T=/home/user/GTA-6-Claude-v0.5; [ $V = fix ] && T=$S/rf/t2
  wmem 2500
  cd $T && nice -n 5 g++ -std=c++17 -O2 -I. tools/worldcheck.cpp -o /tmp/city/wc_rf_$V -lpthread > /tmp/city/wc_rf_${V}_build.log 2>&1; st "worldcheck $V build rc=$?"
  wmem 2000
  cd $T && nice -n 5 /tmp/city/wc_rf_$V 40 > /tmp/city/wc_rf_$V.txt 2>&1; st "worldcheck $V rc=$?: $(grep -iE 'worldcheck: (passed|FAILED)' /tmp/city/wc_rf_$V.txt | head -1)"
done
sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/wc_rf_base.txt > /tmp/city/wc_rf_base_m.txt
sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/wc_rf_fix.txt > /tmp/city/wc_rf_fix_m.txt
diff /tmp/city/wc_rf_base_m.txt /tmp/city/wc_rf_fix_m.txt > /tmp/city/wc_rf_diff.txt; st "worldcheck diff lines: $(grep -c '^[<>]' /tmp/city/wc_rf_diff.txt)"
st "cq61 done"
touch /tmp/city/cq61.done
