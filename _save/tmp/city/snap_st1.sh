#!/bin/bash
# stage 0 step 1 (the terrain bake as data) into the shared tree from $S/s0step1: sites.h/.cpp over HEAD's copies (guarded),
# the new files where none exist yet; blobs with git hash-object -w; MinGW syntax in the shared tree.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
for f in src/world/sites.h src/world/sites.cpp; do
  if [ "$(git hash-object $f)" != "$(git rev-parse HEAD:$f)" ]; then echo "shared $f differs from HEAD: stop"; exit 1; fi
done
NEW="src/world/gridcodec.h src/world/gridcodec.cpp tools/terrainbake.cpp world/terrain/terrain.ntw world/terrain/height.ntg world/terrain/water.ntg world/terrain/region.ntg world/terrain/coast.ntg"
for f in $NEW; do if [ -e "$f" ]; then echo "shared $f exists already: stop"; exit 1; fi; done
echo "shared copies equal HEAD $(git rev-parse --short HEAD); new files absent"
mkdir -p world/terrain
for f in src/world/sites.h src/world/sites.cpp $NEW; do cp $S/s0step1/$f $f; done
for f in src/world/sites.h src/world/sites.cpp; do echo "$f $(git hash-object -w $f) (HEAD $(git rev-parse --short HEAD:$f))"; done | tee /tmp/city/st1_blobs.txt
for f in $NEW; do echo "$f $(git hash-object -w $f) (new, $(stat -c %s $f) bytes)"; done | tee -a /tmp/city/st1_blobs.txt
while [ "$(sh tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_shared_st1.log 2>&1
echo "syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_shared_st1.log) warnings=$(grep -c 'warning' /tmp/city/syntax_shared_st1.log)"
