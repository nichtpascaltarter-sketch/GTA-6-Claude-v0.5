#!/bin/bash
# stage 0 step 2 into the shared tree from $S/s0. The four files with other pending versions (app.cpp, main.cpp, build.sh,
# build.bat) are NOT copied: the lead 3-way merges them at snapshot time from my blobs. cellgen.cpp goes over HEAD's copy
# (guarded) and the 7 new files where none exist; the shared tree stays buildable without the four. Blobs with
# git hash-object -w; MinGW syntax in the shared tree.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
MERGE="src/game/app.cpp src/main.cpp build.sh build.bat"
COPY="src/world/cellgen.cpp"
NEW="src/world/ntw.h src/world/ntw.cpp src/world/worldbin.h src/world/worldbin.cpp src/world/handworld.h src/world/handworld.cpp tools/worldc.cpp"
# s0 was made on 1a83e24: HEAD's copies of every changed file must still be those
for f in $MERGE $COPY; do
  if [ "$(git rev-parse HEAD:$f)" != "$(git rev-parse 1a83e24:$f)" ]; then echo "HEAD's $f changed since 1a83e24: rebase first"; exit 1; fi
done
[ "$(git rev-parse HEAD:src/game/app.cpp)" = "17333c63acf59680f8937e14cb6f92c62754d513" ] || { echo "HEAD's app.cpp is not 17333c63: stop"; exit 1; }
if [ "$(git hash-object $COPY)" != "$(git rev-parse HEAD:$COPY)" ]; then echo "shared $COPY differs from HEAD: stop"; exit 1; fi
for f in $NEW; do if [ -e "$f" ]; then echo "shared $f exists already: stop"; exit 1; fi; done
echo "HEAD $(git rev-parse --short HEAD); bases as at 1a83e24; shared cellgen.cpp = HEAD; new files absent"
for f in $COPY $NEW; do cp $S/s0/$f $f; done
for f in $MERGE; do echo "$f $(git hash-object -w $S/s0/$f) (on HEAD $(git rev-parse --short HEAD:$f); for your merge, not copied)"; done | tee /tmp/city/st2_blobs.txt
for f in $COPY; do echo "$f $(git hash-object -w $f) (HEAD $(git rev-parse --short HEAD:$f))"; done | tee -a /tmp/city/st2_blobs.txt
for f in $NEW; do echo "$f $(git hash-object -w $f) (new, $(stat -c %s $f) bytes)"; done | tee -a /tmp/city/st2_blobs.txt
while [ "$(sh tools/memfree.sh)" -lt 2000 ]; do sleep 15; done
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_shared_st2.log 2>&1
echo "syntax rc=$? errors=$(grep -c 'error' /tmp/city/syntax_shared_st2.log) warnings=$(grep -c 'warning' /tmp/city/syntax_shared_st2.log)"
