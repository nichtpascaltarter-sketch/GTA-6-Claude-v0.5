#!/bin/bash
# batch 6 (Keys motels, painted trim, eyebrow conch houses) in $S/dev8 (= batch 5 + batch 6): MinGW check, nt_dev8.exe
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/jobs8.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
cd $S/dev8 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
waitmem 2500
cd $S/dev8 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev8.log 2>&1
if grep -q "error" /tmp/city/syntax_dev8.log; then st "dev8 syntax errors"; exit 1; fi
st "dev8 syntax ok"
rm -f /tmp/city/nt_dev8.exe
cd $S/dev8 && { time OUT=/tmp/city/nt_dev8.exe sh ./build.sh ; } > /tmp/city/build_dev8.log 2>&1; st "dev8 build rc=$?"
[ -f /tmp/city/nt_dev8.exe ] && st "dev8 exe ready" || st "no dev8 exe"
