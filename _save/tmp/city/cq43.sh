#!/bin/bash
# Redo of cq42's step-4 part (s2c had no build/ directory) and of the CRLF test with each tree's own world/: worldc on
# LF, CRLF and BOM+CRLF copies of that tree's terrain must write byte-identical world.bin. Waits for cq42's exe build.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
T=$S/s0test
st() { echo "$1 $(date)" >> /tmp/city/s1c.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq43 start"
while ! grep -q "nt_s1c build rc=\|cq42: no exe" /tmp/city/s1c.status; do sleep 20; done
mkdir -p $S/s2c/build/gen
cd $S/s2c
wmem 2000
nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/worldc.cpp -o build/worldc > $T/worldc_s2c.log 2>&1; st "s2c worldc build rc=$? ($(grep -c 'warning:' $T/worldc_s2c.log) warnings)"
./build/worldc --world=world --out=build/gen/world.bin > $T/worldc_s2c_run.log 2>&1; st "s2c worldc run rc=$?: $(tail -1 $T/worldc_s2c_run.log)"
wmem 2000
nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/ntwfmt.cpp -o build/ntwfmt > $T/ntwfmt_s2c.log 2>&1; st "s2c ntwfmt build rc=$? ($(grep -c 'warning:' $T/ntwfmt_s2c.log) warnings)"
./build/ntwfmt --check world > $T/ntwfmt_s2c_world.log 2>&1; st "s2c ntwfmt --check world rc=$?: $(tail -1 $T/ntwfmt_s2c_world.log)"
./build/ntwfmt --check $T/samples > $T/ntwfmt_samples.log 2>&1; st "s2c ntwfmt --check samples rc=$?: $(tail -1 $T/ntwfmt_samples.log)"
./build/ntwfmt --check $T/docex > $T/ntwfmt_docex.log 2>&1; st "s2c ntwfmt --check doc examples rc=$?: $(tail -1 $T/ntwfmt_docex.log)"
for TREE in s1c s2c; do
  C=$S/crlftest/$TREE; rm -rf $C; mkdir -p $C/crlf/terrain $C/bomcrlf/terrain
  cp $S/$TREE/world/terrain/*.ntg $C/crlf/terrain/; cp $S/$TREE/world/terrain/*.ntg $C/bomcrlf/terrain/
  sed 's/$/\r/' $S/$TREE/world/terrain/terrain.ntw > $C/crlf/terrain/terrain.ntw
  { printf '\xef\xbb\xbf'; cat $C/crlf/terrain/terrain.ntw; } > $C/bomcrlf/terrain/terrain.ntw
  cd $S/$TREE
  ./build/worldc --world=$C/crlf --out=$C/crlf/world.bin > /dev/null 2>&1; r1=$?
  ./build/worldc --world=$C/bomcrlf --out=$C/bomcrlf/world.bin > /dev/null 2>&1; r2=$?
  st "$TREE worldc on its CRLF terrain.ntw ($(grep -c $'\r' $C/crlf/terrain/terrain.ntw) CRs) rc=$r1: $(cmp -s build/gen/world.bin $C/crlf/world.bin && echo identical || echo DIFFERS); BOM+CRLF rc=$r2: $(cmp -s build/gen/world.bin $C/bomcrlf/world.bin && echo identical || echo DIFFERS)"
done
st "world.bin step 3 vs step 4: $(cmp -s $S/s1c/build/gen/world.bin $S/s2c/build/gen/world.bin && echo identical || echo DIFFERS)"
st "cq43 done"
