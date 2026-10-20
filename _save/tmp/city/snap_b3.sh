#!/bin/bash
# batch 3 into the shared tree: check the shared copies equal HEAD, copy the 7 world files from base4 (= b3), record
# the blobs, MinGW syntax check (memory permitting)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
F="buildings.h buildings.cpp buildmesh.cpp facadedetail.cpp cellgen.cpp blockstyle.cpp massing.cpp"
cd /home/user/GTA-6-Claude-v0.5
for f in $F; do
  if [ "$(git hash-object src/world/$f)" != "$(git rev-parse HEAD:src/world/$f)" ]; then echo "shared $f differs from HEAD: stop"; exit 1; fi
done
echo "shared copies equal HEAD $(git rev-parse --short HEAD)"
for f in $F; do cp $S/base4/src/world/$f src/world/$f; done
for f in $F; do echo "$f $(git hash-object src/world/$f) (HEAD $(git rev-parse --short HEAD:src/world/$f))"; done | tee /tmp/city/b3_blobs.txt
while [ "$(sh tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_shared_b3.log 2>&1
echo "syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_shared_b3.log)"
