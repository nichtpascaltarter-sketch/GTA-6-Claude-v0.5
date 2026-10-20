#!/bin/bash
# tests/world/world_format_test.cpp in $S/s2: GCC -Wall -Wextra -Werror (as CI's GCC job builds its test), run; clang scan.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/s2.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
cd $S/s2 && nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -Werror tests/world/world_format_test.cpp -o $S/s0test/world_format_test > $S/s0test/wft_build.log 2>&1; st "world_format_test build (-Werror) rc=$?"
cd $S/s2 && $S/s0test/world_format_test > $S/s0test/wft_run.log 2>&1; st "world_format_test run rc=$?: $(tail -1 $S/s0test/wft_run.log)"
cd $S/s2 && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wall -Wextra -Wvla -Wgnu -Wpedantic tests/world/world_format_test.cpp > /tmp/city/msvcproxy_wft.log 2>&1; st "clang scan world_format_test: $(grep -c 'warning:\|error:' /tmp/city/msvcproxy_wft.log) diagnostics"
rm -f $S/s0test/world_format_test
