#!/bin/bash
# Step 4 ($S/s2c) after the reference table (schemaRefs) and the lift door word:
# a 16th sample with a dogleg, a spiral, a revolving door and a lift): ntwfmt rebuilt, samples + world + the docs'
# examples --check, world_format_test with CI's flags (timed), clang, the doc-vs-schema table check.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
T=$S/s0test
st() { echo "$1 $(date)" >> /tmp/city/s2c.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
now() { date +%s.%N; }
dt() { awk "BEGIN{printf \"%.1f\", $2 - $1}"; }
st "cq45 start"
cd $S/s2c && mkdir -p build/gen
wmem 2000
nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/ntwfmt.cpp -o build/ntwfmt > $T/ntwfmt_s2c.log 2>&1; st "s2c ntwfmt build rc=$? ($(grep -c 'warning:' $T/ntwfmt_s2c.log) warnings)"
python3 $T/doc_examples.py $T/docex $S/hand0/docs/HANDBUILT_WORLD.md $S/s2c/docs/NTW_FORMAT.md > /dev/null
./build/ntwfmt --check world > $T/ntwfmt_s2c_world.log 2>&1; st "s2c ntwfmt --check world rc=$?: $(tail -1 $T/ntwfmt_s2c_world.log)"
./build/ntwfmt --check $T/samples > $T/ntwfmt_samples.log 2>&1; st "s2c ntwfmt --check samples rc=$?: $(tail -1 $T/ntwfmt_samples.log)"
./build/ntwfmt --check $T/docex > $T/ntwfmt_docex.log 2>&1; st "s2c ntwfmt --check doc examples rc=$?: $(tail -1 $T/ntwfmt_docex.log)"
st "s2c $(python3 $T/doccheck.py $S/s2c | tail -1) (the known 2: stops and value are named without a type word)"
wmem 2500
t0=$(now); nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=undefined tests/world/world_format_test.cpp -o /tmp/city/neontide-world-format-test-s2c > $T/wft_s2c.log 2>&1; rc=$?; t1=$(now)
/tmp/city/neontide-world-format-test-s2c > $T/wft_s2c_run.log 2>&1; rc2=$?; t2=$(now)
st "s2c world_format_test (CI flags) build rc=$rc in $(dt $t0 $t1) s, run rc=$rc2 in $(dt $t1 $t2) s: $(tail -1 $T/wft_s2c_run.log)"
rm -f /tmp/city/neontide-world-format-test-s2c
nice -n 5 clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Wvla -Wgnu -Wpedantic tests/world/world_format_test.cpp > $T/clang_wft_s2c.log 2>&1
nice -n 5 clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Wvla -Wgnu -Wpedantic -I. tools/worldc.cpp > $T/clang_worldc_s2c.log 2>&1
st "s2c clang scans: test $(grep -c 'warning:\|error:' $T/clang_wft_s2c.log), worldc $(grep -c 'warning:\|error:' $T/clang_worldc_s2c.log) diagnostics"
wmem 2000
nice -n 5 g++ -O2 -std=c++17 -Wall -Wextra -I. tools/worldc.cpp -o build/worldc > $T/worldc_s2c.log 2>&1; st "s2c worldc build rc=$? ($(grep -c 'warning:' $T/worldc_s2c.log) warnings)"
./build/worldc --world=world --out=build/gen/world.bin > $T/worldc_s2c_run.log 2>&1; st "s2c worldc run rc=$?: $(tail -1 $T/worldc_s2c_run.log); vs step 3: $(cmp -s build/gen/world.bin $S/s1c/build/gen/world.bin && echo identical || echo DIFFERS)"
st "cq45 done"
