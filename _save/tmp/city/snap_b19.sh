#!/bin/bash
# batch 19 into the shared tree from $S/dev27: the four MAT_PLASTIC call sites; check the shared copies equal HEAD's, copy,
# write the blobs, MinGW in the shared tree
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
F="src/world/cemetery.cpp src/world/outskirts.cpp src/world/transitmesh.cpp src/world/transitmesh_tram.cpp"
for f in $F; do
  if [ "$(git hash-object $f)" != "$(git rev-parse HEAD:$f)" ]; then echo "shared $f differs from HEAD: stop"; exit 1; fi
done
echo "shared copies equal HEAD $(git rev-parse --short HEAD)"
for f in $F; do cp $S/dev27/$f $f; done
for f in $F; do echo "$f $(git hash-object -w $f) (HEAD $(git rev-parse --short HEAD:$f))"; done | tee /tmp/city/b19_blobs.txt
while [ "$(sh tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_shared_b19.log 2>&1
echo "syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_shared_b19.log) warnings=$(grep -c 'warning' /tmp/city/syntax_shared_b19.log)"
