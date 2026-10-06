#!/bin/bash
# batch 6 (yards) on main 8ee5699 + batch 4: MinGW check, nt_dev6.exe, then (after jobsD) in-game shots of the yards
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
L=/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt
st() { echo "$1 $(date)" >> /tmp/city/jobs6.status; }
waitmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
cd $S/dev6 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
waitmem 2500
cd $S/dev6 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev6.log 2>&1
if grep -q "error" /tmp/city/syntax_dev6.log; then st "dev6 syntax errors"; exit 1; fi
st "dev6 syntax ok"
rm -f /tmp/city/nt_dev6.exe
cd $S/dev6 && { time OUT=/tmp/city/nt_dev6.exe sh ./build.sh ; } > /tmp/city/build_dev6.log 2>&1; st "dev6 build rc=$?"
[ -f /tmp/city/nt_dev6.exe ] || { st "no dev6 exe"; exit 1; }
st "dev6 exe ready"
