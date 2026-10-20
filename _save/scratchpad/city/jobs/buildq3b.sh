#!/bin/bash
# rebuild the batch-3 exe (storefront glass fix), MinGW syntax check first
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
cd $S/b3 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_b3.log 2>&1
if grep -q "error" /tmp/city/syntax_b3.log; then echo "b3 syntax errors $(date)" >> /tmp/city/buildq.status; exit 1; fi
cd $S/b3 && { time OUT=/tmp/city/nt_b3.exe sh ./build.sh ; } > /tmp/city/build_b3.log 2>&1
echo "b3 build done $(date)" >> /tmp/city/buildq.status
