#!/bin/bash
# AI agent: build exactly a graft - a commit's tree (git archive: src, tools, build.sh) with files replaced by the blobs
# in a list ("BLOB path" lines) and the AI peds.cpp blocks pasted where missing (ai_hands.py: before the foot IK line) -
# into the main bin/. Records the tree's blobs (AW/blobs_TAG.txt). Usage: ai_buildgraft.sh COMMIT BLOBLIST OUTEXE
MAIN=/home/user/GTA-6-Claude-v0.5
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
COMMIT=$1
LIST=$2
OUTEXE=$3
TAG=$(basename $OUTEXE .exe)
T=$SP/tree_$TAG
rm -rf $T; mkdir -p $T/build $T/bin
git -C $MAIN archive $COMMIT src tools build.sh | tar -x -C $T || exit 5
mkdir -p $T/build/gen && cp -a $MAIN/build/embed_shaders $T/build/ && (cd $T && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h > /dev/null) || exit 7
while read -r B F; do
  [ -z "$B" ] && continue
  git -C $MAIN cat-file -p $B > $T/$F || exit 6
done < $LIST
python3 $AW/ai_hands.py $T/src/game/peds.cpp || exit 4
for f in ai.cpp ai_core.h ai_game.h app.cpp barks.cpp events.cpp gameworld.h pedai.cpp pednav.cpp police.cpp traffic.cpp traffic_core.cpp lanes.cpp population.cpp peds.cpp game.h wildlife.cpp wildlife.h; do
  echo "$(git -C $MAIN hash-object -w $T/src/game/$f) src/game/$f"
done > $AW/blobs_$TAG.txt
echo "$(date +%T) grafted the tree for $TAG on $COMMIT"
LOG=$AW/ai_buildloop_$TAG.log
MINE='game/(ai|police|lanes|traffic_core|pednav|barks|traffic|pedai|events|app|wildlife)\.(cpp|h)|ai_core\.h|ai_game\.h'
cd $T
for i in $(seq 1 40); do
  $AW/ai_memgate.sh 2000
  SY=$(nice -n 5 x86_64-w64-mingw32-g++ -std=c++17 -O0 -fsyntax-only --param ggc-min-expand=20 --param ggc-min-heapsize=32768 -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen src/main.cpp 2>&1 | grep -E "error|warning" | head -8)
  if [ -n "$SY" ]; then
    if echo "$SY" | grep -qE "$MINE"; then echo "MINE (syntax): $SY"; exit 2; fi
    echo "$(date +%T) foreign break in the grafted tree: $(echo "$SY" | head -1)"; exit 3
  fi
  $AW/ai_memgate.sh 3200
  QUICK=${QUICK:-1} OUT=bin/$TAG.exe nice -n 5 ./build.sh > $LOG 2>&1
  if [ $? -eq 0 ]; then mv -f bin/$TAG.exe $MAIN/$OUTEXE && cd $SP && rm -rf $T && echo "$(date +%T) built $OUTEXE after $i tries"; exit 0; fi
  ERR=$(grep -E "error|undefined|Killed" $LOG | head -6)
  if echo "$ERR" | grep -qE "$MINE"; then echo "MINE (build): $ERR"; exit 2; fi
  echo "$(date +%T) try $i build failed (OOM?): $(echo "$ERR" | head -2)"; sleep 60
done
exit 1
