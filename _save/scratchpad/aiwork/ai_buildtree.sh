#!/bin/bash
# AI agent: freeze the working tree (src, tools, build.sh, build/gen) into a scratch copy, record the AI files' blobs
# from that copy, and build there - the main tree stays free for editing meanwhile. The exe lands in the main bin/.
# Stops early on errors in AI-owned files; retries foreign breaks / OOM kills. Usage: ai_buildtree.sh OUTEXE
MAIN=/home/user/GTA-6-Claude-v0.5
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
OUTEXE=$1
TAG=$(basename $OUTEXE .exe)
T=$SP/tree_$TAG
echo $$ > $AW/ai_buildloop.pid
rm -rf $T; mkdir -p $T/build $T/bin
cp -a $MAIN/src $MAIN/tools $MAIN/build.sh $T/ && cp -a $MAIN/build/gen $T/build/ && cp -a $MAIN/build/embed_shaders $T/build/ 2>/dev/null
# HANDS=1: the AI hand blocks pasted into this copy's peds.cpp (test builds; the lead grafts them for real)
[ -n "$HANDS" ] && { python3 $AW/ai_hands.py $T/src/game/peds.cpp || exit 4; }
for f in ai.cpp ai_core.h ai_game.h app.cpp barks.cpp events.cpp gameworld.h pedai.cpp pednav.cpp police.cpp traffic.cpp traffic_core.cpp lanes.cpp population.cpp peds.cpp game.h; do
  echo "$(git -C $MAIN hash-object -w $T/src/game/$f) src/game/$f"
done > $AW/blobs_$TAG.txt
echo "$(date +%T) froze the tree for $TAG"
LOG=$AW/ai_buildloop.log
MINE='game/(ai|police|lanes|traffic_core|pednav|barks|traffic|pedai|events|app)\.(cpp|h)|ai_core\.h|ai_game\.h'
cd $T
for i in $(seq 1 40); do
  $AW/ai_memgate.sh 2000
  SY=$(nice -n 5 x86_64-w64-mingw32-g++ -std=c++17 -O0 -fsyntax-only --param ggc-min-expand=20 --param ggc-min-heapsize=32768 -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen src/main.cpp 2>&1 | grep -E "error|warning" | head -8)
  if [ -n "$SY" ]; then
    if echo "$SY" | grep -qE "$MINE"; then echo "MINE (syntax): $SY"; exit 2; fi
    echo "$(date +%T) foreign break in the frozen tree: $(echo "$SY" | head -1)"; exit 3
  fi
  $AW/ai_memgate.sh 3200
  QUICK=${QUICK:-1} OUT=bin/$TAG.exe nice -n 5 ./build.sh > $LOG 2>&1
  if [ $? -eq 0 ]; then mv -f bin/$TAG.exe $MAIN/$OUTEXE && cd $SP && rm -rf $T && echo "$(date +%T) built $OUTEXE after $i tries"; exit 0; fi
  ERR=$(grep -E "error|undefined|Killed" $LOG | head -6)
  if echo "$ERR" | grep -qE "$MINE"; then echo "MINE (build): $ERR"; exit 2; fi
  echo "$(date +%T) try $i build failed (OOM?): $(echo "$ERR" | head -2)"; sleep 60
done
exit 1
