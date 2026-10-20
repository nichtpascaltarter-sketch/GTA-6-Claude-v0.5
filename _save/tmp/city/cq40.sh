#!/bin/bash
# Host checks of step 3 ($S/s1b) and step 4 ($S/s2b), both rebased onto the reviewed step 2 ($S/s0b): worldc (-Wall
# -Wextra) and its world.bin, the load test, ntwfmt --check (world/, the samples, the docs' examples), world_format_test
# exactly as CI's GCC job would build it (UBSan, -Werror), with times; clang scans; MinGW syntax of the step-3 game.
# Waits for cq39's exe build first (one build of mine at a time).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
T=$S/s0test
st() { echo "$1 $(date)" >> /tmp/city/s1b.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
now() { date +%s.%N; }
dt() { awk "BEGIN{printf \"%.1f\", $2 - $1}"; }
st "cq40 start"
while ! grep -q "nt_s0b build rc=" /tmp/city/s0b.status 2>/dev/null && pgrep -f /tmp/city/cq39.sh > /dev/null; do sleep 20; done
st "cq39's build is done"
for TREE in s1b s2b; do
  cd $S/$TREE && mkdir -p build/gen
  wmem 2000
  nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/worldc.cpp -o build/worldc > $T/worldc_$TREE.log 2>&1; st "$TREE worldc build rc=$? ($(grep -c 'warning:' $T/worldc_$TREE.log) warnings)"
  ./build/worldc --world=world --out=build/gen/world.bin > $T/worldc_${TREE}_run.log 2>&1; st "$TREE worldc run rc=$?: $(tail -1 $T/worldc_${TREE}_run.log)"
  wmem 2000
  nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/ntwfmt.cpp -o build/ntwfmt > $T/ntwfmt_$TREE.log 2>&1; st "$TREE ntwfmt build rc=$? ($(grep -c 'warning:' $T/ntwfmt_$TREE.log) warnings)"
  ./build/ntwfmt --check world > $T/ntwfmt_${TREE}_world.log 2>&1; st "$TREE ntwfmt --check world rc=$?: $(tail -1 $T/ntwfmt_${TREE}_world.log)"
  wmem 2500
  t0=$(now); nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined tests/world/world_format_test.cpp -o /tmp/city/neontide-world-format-test-$TREE > $T/wft_$TREE.log 2>&1; rc=$?; t1=$(now)
  /tmp/city/neontide-world-format-test-$TREE > $T/wft_${TREE}_run.log 2>&1; rc2=$?; t2=$(now)
  st "$TREE world_format_test (CI flags) build rc=$rc in $(dt $t0 $t1) s, run rc=$rc2 in $(dt $t1 $t2) s: $(tail -1 $T/wft_${TREE}_run.log)"
  rm -f /tmp/city/neontide-world-format-test-$TREE
  nice -n 5 clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Wvla -Wgnu -Wpedantic tests/world/world_format_test.cpp > $T/clang_wft_$TREE.log 2>&1
  nice -n 5 clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Wvla -Wgnu -Wpedantic -I. tools/worldc.cpp > $T/clang_worldc_$TREE.log 2>&1
  nice -n 5 clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Wvla -Wgnu -Wpedantic -I. tools/ntwfmt.cpp > $T/clang_ntwfmt_$TREE.log 2>&1
  st "$TREE clang scans: test $(grep -c 'warning:\|error:' $T/clang_wft_$TREE.log), worldc $(grep -c 'warning:\|error:' $T/clang_worldc_$TREE.log), ntwfmt $(grep -c 'warning:\|error:' $T/clang_ntwfmt_$TREE.log) diagnostics"
done
cd $S/s2b && ./build/ntwfmt --check $T/samples > $T/ntwfmt_samples.log 2>&1; st "s2b ntwfmt --check samples rc=$?: $(tail -1 $T/ntwfmt_samples.log)"
cd $S/s2b && ./build/ntwfmt --check $T/docex > $T/ntwfmt_docex.log 2>&1; st "s2b ntwfmt --check doc examples rc=$?: $(tail -1 $T/ntwfmt_docex.log)"
cmp $S/s1b/build/gen/world.bin $S/s2b/build/gen/world.bin > /dev/null 2>&1; st "world.bin step 3 vs step 4: $([ $? = 0 ] && echo identical || echo DIFFERS)"
wmem 2000
cd $S/s1b && nice -n 5 g++ -std=c++17 -O2 -I. $T/handload.cpp -o $T/handload_s1b -lpthread > $T/handload_s1b_build.log 2>&1; st "s1b handload build rc=$?"
wmem 1500
cd $S/s1b && $T/handload_s1b build/gen/world.bin > $T/handload_s1b_run.log 2>&1; st "s1b handload run rc=$?: $(tail -1 $T/handload_s1b_run.log); $(grep 'damaged table' $T/handload_s1b_run.log)"
rm -f $T/handload_s1b
cd $S/s1b && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
wmem 2500
cd $S/s1b && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_s1b.log 2>&1; st "s1b game syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_s1b.log) warnings=$(grep -c 'warning' /tmp/city/syntax_s1b.log)"
st "cq40 done"
