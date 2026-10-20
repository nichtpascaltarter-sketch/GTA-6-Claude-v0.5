#!/bin/bash
# my builds, one at a time: the batch-3 exe once my native worldcheck job is done (MinGW syntax check first)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while ! grep -q "^exit" /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/tasks/bj4t5ow3j.output 2>/dev/null; do sleep 20; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
cd $S/b3 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_b3.log 2>&1
if grep -q "error" /tmp/city/syntax_b3.log; then echo "b3 syntax errors $(date)" >> /tmp/city/buildq.status; exit 1; fi
cd $S/b3 && { time OUT=/tmp/city/nt_b3.exe sh ./build.sh ; } > /tmp/city/build_b3.log 2>&1
echo "b3 build done $(date)" >> /tmp/city/buildq.status
