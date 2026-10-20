#!/bin/bash
# GNU-extension scan of the world code (clang -Wgnu -Wvla -Wpedantic) as an MSVC proxy: dev22 = batches 11 + 13 + 12 + 14
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/msvcproxy.status; }
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
cd $S/wc && nice -n 5 clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -Wno-unused -I$S/dev22 worldcheck.cpp > /tmp/city/msvcproxy_dev22.log 2>&1
st "clang scan exit $?: $(grep -c 'warning:' /tmp/city/msvcproxy_dev22.log) warnings, $(grep -c 'error:' /tmp/city/msvcproxy_dev22.log) errors; in my files: $(grep -E 'src/world/(buildings\.h|buildings\.cpp|buildmesh\.cpp|blockstyle\.cpp|massing\.cpp|facadedetail\.cpp|propmesh\.cpp):[0-9]+:[0-9]+: (warning|error)' /tmp/city/msvcproxy_dev22.log | wc -l)"
