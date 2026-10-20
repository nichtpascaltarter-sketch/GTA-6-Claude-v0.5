#!/bin/bash
# batch 11 into the shared tree: the 4 world files over HEAD's (checked equal first), blobs, MinGW syntax check
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5 || exit 1
F="src/world/buildings.h src/world/buildings.cpp src/world/buildmesh.cpp src/world/blockstyle.cpp"
git diff --quiet HEAD -- $F || { echo "shared copies differ from HEAD: stop"; git status --short -- $F; exit 1; }
for f in $F; do cp $S/dev15/$f $f; echo "$f $(git hash-object -w $f)"; done
while [ "$(sh tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_shared_b11.log 2>&1
echo "shared syntax exit $?: $(grep -c 'error' /tmp/city/syntax_shared_b11.log) error lines, $(grep -c 'warning' /tmp/city/syntax_shared_b11.log) warnings"
git status --short src/world
