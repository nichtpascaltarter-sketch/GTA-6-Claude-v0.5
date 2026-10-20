#!/bin/bash
# Step 2 after the lead's review: copy the three changed uncontended files into the shared tree (guarded on my previous
# step-2 blobs), and write blobs for the merge-only files (app.cpp, main.cpp, build.sh, build.bat, renderer.cpp).
set -e
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
R=/home/user/GTA-6-Claude-v0.5
cd $R
[ "$(git rev-parse HEAD)" = "$(git rev-parse 0c288db)" ] || { echo "HEAD moved: $(git log --oneline -1)"; exit 1; }
guard() { [ "$(git hash-object $1)" = "$2" ] || { echo "GUARD: $1 is $(git hash-object $1), expected $2"; exit 1; }; }
guard src/world/worldbin.h 28e6ab2f9d9233170a261d70ce149e434a5c03ee
guard src/world/worldbin.cpp 6dce0152e91404aa3e474676e2f51282251bc554
guard tools/worldc.cpp 490d0c6650475be821a401f34ad6fd76d89de3ae
for f in src/game/app.cpp src/main.cpp build.sh build.bat src/render/renderer.cpp; do
  [ "$(git hash-object $f)" = "$(git rev-parse HEAD:$f)" ] || { echo "GUARD: shared $f differs from HEAD"; exit 1; }
done
for f in src/world/worldbin.h src/world/worldbin.cpp tools/worldc.cpp; do cp $S/s0b/$f $R/$f; done
: > /tmp/city/st2b_blobs.txt
for f in src/game/app.cpp src/main.cpp build.sh build.bat src/render/renderer.cpp; do
  b=$(git hash-object -w $S/s0b/$f)
  echo "$f $b (on HEAD $(git rev-parse HEAD:$f | cut -c1-8); for your merge, not copied)" >> /tmp/city/st2b_blobs.txt
done
for f in src/world/cellgen.cpp src/world/ntw.h src/world/ntw.cpp src/world/worldbin.h src/world/worldbin.cpp src/world/handworld.h src/world/handworld.cpp tools/worldc.cpp; do
  b=$(git hash-object -w $R/$f)
  [ "$b" = "$(git hash-object $S/s0b/$f)" ] || { echo "COPY MISMATCH $f"; exit 1; }
  if git cat-file -e HEAD:$f 2>/dev/null; then echo "$f $b (HEAD $(git rev-parse HEAD:$f | cut -c1-8); in the shared tree)" >> /tmp/city/st2b_blobs.txt
  else echo "$f $b (new, $(wc -c < $R/$f) bytes; in the shared tree)" >> /tmp/city/st2b_blobs.txt; fi
done
cat /tmp/city/st2b_blobs.txt
