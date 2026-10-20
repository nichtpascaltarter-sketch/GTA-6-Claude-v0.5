#!/bin/bash
# Step 3 ($S/s1d) and step 4 ($S/s2d) on snapshot 59 (HEAD d208e02): MinGW syntax of the step-3 game; worldc (-Wall
# -Wextra) and world.bin vs the s1c build; ntwfmt --check (world, samples, the docs' examples); world_format_test with
# CI's GCC flags (timed); clang scans; the doc-vs-schema table check.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
T=$S/s0test
st() { echo "$1 $(date)" >> /tmp/city/s1d.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
now() { date +%s.%N; }
dt() { awk "BEGIN{printf \"%.1f\", $2 - $1}"; }
st "cq46 start"
cd $S/s1d && mkdir -p build/gen && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h > /dev/null
wmem 2500
cd $S/s1d && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_s1d.log 2>&1; st "s1d game syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_s1d.log) warnings=$(grep -c 'warning' /tmp/city/syntax_s1d.log)"
python3 $T/doc_examples.py $T/docex $S/s2d/docs/HANDBUILT_WORLD.md $S/s2d/docs/NTW_FORMAT.md > /dev/null
for TREE in s1d s2d; do
  cd $S/$TREE && mkdir -p build/gen
  wmem 2000
  nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/worldc.cpp -o build/worldc > $T/worldc_$TREE.log 2>&1; st "$TREE worldc build rc=$? ($(grep -c 'warning:' $T/worldc_$TREE.log) warnings)"
  ./build/worldc --world=world --out=build/gen/world.bin > $T/worldc_${TREE}_run.log 2>&1; st "$TREE worldc run rc=$?: $(tail -1 $T/worldc_${TREE}_run.log); vs s1c: $(cmp -s build/gen/world.bin $S/s1c/build/gen/world.bin && echo identical || echo DIFFERS)"
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
cd $S/s2d && ./build/ntwfmt --check $T/samples > $T/ntwfmt_samples.log 2>&1; st "s2d ntwfmt --check samples rc=$?: $(tail -1 $T/ntwfmt_samples.log)"
cd $S/s2d && ./build/ntwfmt --check $T/docex > $T/ntwfmt_docex.log 2>&1; st "s2d ntwfmt --check doc examples rc=$?: $(tail -1 $T/ntwfmt_docex.log)"
st "s2d $(python3 $T/doccheck.py $S/s2d | tail -1) (the known 2)"
st "cq46 done"
