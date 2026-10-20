#!/bin/bash
# Step 3 on snapshot 59 (HEAD d208e02): copy into the shared tree only the new files nothing includes yet; write blobs
# for everything (the changed files need main.cpp's lz.cpp include and build.sh's WORLDC_SRC, which the lead merges).
set -e
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
R=/home/user/GTA-6-Claude-v0.5
cd $R
[ "$(git rev-parse HEAD)" = "$(git rev-parse d208e02)" ] || { echo "HEAD moved: $(git log --oneline -1)"; exit 1; }
NEW="src/world/lz.h src/world/lz.cpp src/world/ntwschema.h src/world/ntwschema.cpp tools/ntwfmt.cpp tests/world/world_format_test.cpp"
CHANGED="src/main.cpp build.sh src/world/ntw.h src/world/ntw.cpp src/world/worldbin.h src/world/worldbin.cpp src/world/handworld.cpp tools/worldc.cpp tools/terrainbake.cpp world/terrain/terrain.ntw"
for f in $NEW; do [ ! -e $f ] && ! git cat-file -e HEAD:$f 2>/dev/null || { echo "GUARD: $f exists"; exit 1; }; done
for f in $CHANGED; do [ "$(git hash-object $f)" = "$(git rev-parse HEAD:$f)" ] || { echo "GUARD: shared $f differs from HEAD"; exit 1; }; done
for f in $NEW; do mkdir -p $(dirname $R/$f); cp $S/s1d/$f $R/$f; done
: > /tmp/city/st3_blobs.txt
for f in $CHANGED; do
  b=$(git hash-object -w $S/s1d/$f)
  echo "$f $b (on HEAD $(git rev-parse HEAD:$f | cut -c1-8); not copied: lands with the others)" >> /tmp/city/st3_blobs.txt
done
for f in $NEW; do
  b=$(git hash-object -w $R/$f)
  [ "$b" = "$(git hash-object $S/s1d/$f)" ] || { echo "COPY MISMATCH $f"; exit 1; }
  echo "$f $b (new, $(wc -c < $R/$f) bytes; in the shared tree)" >> /tmp/city/st3_blobs.txt
done
cat /tmp/city/st3_blobs.txt
