#!/bin/bash
# after the schema helper fix (optional word fields): ntwtest and ntwfmt again in $S/s1 (step 3) and $S/s2 (step 4), the
# per-kind samples, the canonical terrain.ntw. After cq36's build (one build of mine at a time).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/s2.status; }
until grep -q "^nt_s1 build rc" /tmp/city/s1.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/cq36.sh" > /dev/null; do sleep 20; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
st "cq37 start"
for T in s1 s2; do
  cd $S/$T && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. $S/s0test/ntwtest.cpp -o $S/s0test/ntwtest_$T > /dev/null 2>&1; $S/s0test/ntwtest_$T > $S/s0test/ntwtest_${T}_b.log 2>&1; st "$T ntwtest rc=$? ($(tail -1 $S/s0test/ntwtest_${T}_b.log))"; rm -f $S/s0test/ntwtest_$T
  cd $S/$T && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/ntwfmt.cpp -o build/ntwfmt > $S/s0test/ntwfmt_${T}_b.log 2>&1; st "$T ntwfmt build rc=$? ($(grep -c warning $S/s0test/ntwfmt_${T}_b.log) warnings)"
  cd $S/$T && ./build/ntwfmt --check world > $S/s0test/world_fmt_${T}_b.log 2>&1; st "$T ntwfmt --check world rc=$?: $(tail -1 $S/s0test/world_fmt_${T}_b.log)"
done
rm -rf $S/s0test/samples_fmt && cp -r $S/s0test/samples $S/s0test/samples_fmt
cd $S/s2 && ./build/ntwfmt $S/s0test/samples_fmt > $S/s0test/samples_fmt1.log 2>&1; st "s2 ntwfmt on the samples rc=$?: $(tail -1 $S/s0test/samples_fmt1.log)"
cd $S/s2 && ./build/ntwfmt --check $S/s0test/samples_fmt > $S/s0test/samples_fmt2.log 2>&1; st "s2 ntwfmt --check formatted samples rc=$?: $(tail -1 $S/s0test/samples_fmt2.log)"
cd $S/s2 && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o build/worldc > /dev/null 2>&1 && ./build/worldc > $S/s0test/worldc_s2_b.log 2>&1; st "s2 worldc rc=$?: $(tail -1 $S/s0test/worldc_s2_b.log)"
st "cq37 done"
