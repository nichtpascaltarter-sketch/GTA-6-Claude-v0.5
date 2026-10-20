#!/bin/bash
# stage 0 step 4 in $S/s2 (the full schema + docs/NTW_FORMAT.md): ntwfmt over one sample file per kind (schema errors,
# canonical form, idempotence), the step 3 unit tests again, ntwfmt and worldc on world/, clang and MinGW syntax of the
# tools. After cq33.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/s2.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
until grep -q "^cq33 done" /tmp/city/s1.status 2>/dev/null || ! pgrep -f "^/bin/bash /tmp/city/cq33.sh" > /dev/null; do sleep 20; done
st "cq34 start"
wmem 2000
cd $S/s2 && mkdir -p build/gen && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/ntwfmt.cpp -o build/ntwfmt > $S/s0test/ntwfmt4_build.log 2>&1; st "ntwfmt (s2) build rc=$? ($(grep -c warning $S/s0test/ntwfmt4_build.log) warnings)"
rm -rf $S/s0test/samples_fmt && cp -r $S/s0test/samples $S/s0test/samples_fmt
cd $S/s2 && ./build/ntwfmt $S/s0test/samples_fmt > $S/s0test/samples_fmt1.log 2>&1; st "ntwfmt on the samples rc=$?: $(tail -1 $S/s0test/samples_fmt1.log)"
cd $S/s2 && ./build/ntwfmt --check $S/s0test/samples_fmt > $S/s0test/samples_fmt2.log 2>&1; st "ntwfmt --check on the formatted samples rc=$?: $(tail -1 $S/s0test/samples_fmt2.log)"
cd $S/s2 && ./build/ntwfmt --check world > $S/s0test/world_fmt4.log 2>&1; st "ntwfmt --check world (s2) rc=$?: $(tail -1 $S/s0test/world_fmt4.log)"
cd $S/s2 && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. $S/s0test/ntwtest.cpp -o $S/s0test/ntwtest4 > $S/s0test/ntwtest4_build.log 2>&1; st "ntwtest (s2) build rc=$?"
$S/s0test/ntwtest4 > $S/s0test/ntwtest4_run.log 2>&1; st "ntwtest (s2) run rc=$? ($(tail -1 $S/s0test/ntwtest4_run.log))"; rm -f $S/s0test/ntwtest4
cd $S/s2 && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldc.cpp -o build/worldc > $S/s0test/worldc4_build.log 2>&1; st "worldc (s2) build rc=$? ($(grep -c warning $S/s0test/worldc4_build.log) warnings)"
cd $S/s2 && ./build/worldc > $S/s0test/worldc4_run.log 2>&1; st "worldc (s2) run rc=$?: $(tail -1 $S/s0test/worldc4_run.log)"
cd $S/s2 && for t in tools/ntwfmt.cpp tools/worldc.cpp; do nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I. $t >> /tmp/city/msvcproxy_s2.log 2>&1; done; st "clang scans (s2 tools): $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_s2.log) diagnostics"
cd $S/s2 && for t in tools/ntwfmt.cpp tools/worldc.cpp; do nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -I. $t >> /tmp/city/syntax_s2_tools.log 2>&1; done; st "MinGW syntax (s2 tools): $(grep -c 'warning\|error' /tmp/city/syntax_s2_tools.log) diagnostics"
st "cq34 done"
