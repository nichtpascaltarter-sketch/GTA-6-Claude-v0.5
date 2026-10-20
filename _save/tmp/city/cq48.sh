#!/bin/bash
# Road-overlap fix: the census harness (rfcheck) with HEAD's roads.cpp and with the fix, both runs, and the diff.
# Waits for cq47 (one build of mine at a time).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/rf.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
true
st "cq48 start"
cd $S/rf/t
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I. -I src -DRF_BASE $S/rf/rfcheck.cpp -o /tmp/city/rfcheck_base -lpthread > /tmp/city/rfcheck_base_build.log 2>&1; st "rfcheck_base build rc=$?"
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I. -I src $S/rf/rfcheck.cpp -o /tmp/city/rfcheck_fix -lpthread > /tmp/city/rfcheck_fix_build.log 2>&1; st "rfcheck_fix build rc=$?"
for V in base fix; do
  wmem 2000
  cd $S/rf/t && nice -n 5 /tmp/city/rfcheck_$V > /tmp/city/census_$V.txt 2> /tmp/city/census_${V}_err.txt; st "rfcheck_$V run rc=$?: $(head -1 /tmp/city/census_$V.txt)"
done
python3 $S/rf/rfdiff.py /tmp/city/census_base.txt /tmp/city/census_fix.txt /tmp/city/ov_base.txt > /tmp/city/census_diff.txt 2>&1; st "diff rc=$?: $(head -4 /tmp/city/census_diff.txt | tr '\n' ' ')"
st "cq48 done"
