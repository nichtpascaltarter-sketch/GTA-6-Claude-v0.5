#!/bin/bash
# Step 3 on the HEAD that holds step 2 (snapshot 59): build $S/s1d = git archive HEAD + step 3 (from $S/s1c), with
# main.cpp and build.sh 3-way merged onto HEAD's copies; MinGW syntax of s1d. Usage: prep_st3.sh <head sha>
set -e
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
R=/home/user/GTA-6-Claude-v0.5
H=$1
cd $R
[ "$(git rev-parse HEAD)" = "$(git rev-parse $H)" ] || { echo "HEAD is $(git log --oneline -1), not $H"; exit 1; }
# step 2 as I sent it must be in HEAD
for f in src/world/ntw.h src/world/ntw.cpp src/world/worldbin.h src/world/worldbin.cpp src/world/handworld.h src/world/handworld.cpp tools/worldc.cpp src/world/cellgen.cpp .gitattributes src/render/renderer.cpp src/game/app.cpp src/main.cpp build.sh build.bat world/terrain/terrain.ntw tools/terrainbake.cpp; do
  [ "$(git rev-parse HEAD:$f)" = "$(git hash-object $S/s0c/$f)" ] || echo "NOTE: HEAD's $f is $(git rev-parse HEAD:$f | cut -c1-8), step 2 sent $(git hash-object $S/s0c/$f | cut -c1-8)"
done
rm -rf $S/s1d && mkdir $S/s1d && git archive HEAD | tar x -C $S/s1d
echo "s1d = HEAD $(git rev-parse --short HEAD) + step 3" > $S/s1d/.s1d_base
for f in src/world/ntw.h src/world/ntw.cpp src/world/ntwschema.h src/world/ntwschema.cpp src/world/lz.h src/world/lz.cpp src/world/worldbin.h src/world/worldbin.cpp src/world/handworld.cpp tools/worldc.cpp tools/ntwfmt.cpp tools/terrainbake.cpp world/terrain/terrain.ntw tests/world/world_format_test.cpp; do
  if git cat-file -e HEAD:$f 2>/dev/null && [ "$(git rev-parse HEAD:$f)" != "$(git hash-object $S/s0c/$f 2>/dev/null)" ]; then echo "STOP: HEAD's $f is not step 2's"; exit 1; fi
  mkdir -p $(dirname $S/s1d/$f); cp $S/s1c/$f $S/s1d/$f
done
for f in src/main.cpp build.sh; do
  git show HEAD:$f > $S/s1d/$f
  git merge-file $S/s1d/$f $S/s0c/$f $S/s1c/$f || { echo "STOP: $f merge conflict"; exit 1; }
done
chmod +x $S/s1d/build.sh
cd $S/s1d && diff -rq $S/s1c . -x build -x '.s1*' | grep -v "^Only in $S/s1c: build" || true
