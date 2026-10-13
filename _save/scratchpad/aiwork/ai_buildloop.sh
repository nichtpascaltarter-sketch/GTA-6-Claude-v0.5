#!/bin/bash
# AI agent: retry the unity build until it links; stop early on errors in AI-owned files. Usage: ai_buildloop.sh OUTEXE
cd /home/user/GTA-6-Claude-v0.5
# (this loop's PID: ai_killbuild.sh stops it and only its own descendants)
echo $$ > /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork/ai_buildloop.pid
OUTEXE=$1
LOG=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork/ai_buildloop.log
MINE='game/(ai|population|police|lanes|traffic_core|pednav|barks|traffic|pedai|events|app)\.(cpp|h)|ai_core\.h|ai_game\.h'
for i in $(seq 1 40); do
  /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork/ai_memgate.sh 2000
  SY=$(nice -n 5 x86_64-w64-mingw32-g++ -std=c++17 -O0 -fsyntax-only --param ggc-min-expand=20 --param ggc-min-heapsize=32768 -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen src/main.cpp 2>&1 | grep -E "error|warning" | head -8)
  if [ -n "$SY" ]; then
    if echo "$SY" | grep -qE "$MINE"; then echo "MINE (syntax): $SY"; exit 2; fi
    echo "$(date +%T) try $i foreign: $(echo "$SY" | head -1)"; sleep 90; continue
  fi
  /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork/ai_memgate.sh 3200
  # the AI files as compiled into this exe (git blobs: a sign-off names exactly what was tested)
  for f in ai.cpp ai_core.h ai_game.h app.cpp barks.cpp events.cpp gameworld.h pedai.cpp pednav.cpp police.cpp traffic.cpp traffic_core.cpp lanes.cpp population.cpp peds.cpp game.h; do
    echo "$(git hash-object -w src/game/$f) src/game/$f"
  done > /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork/blobs_$(basename $OUTEXE .exe).txt
  QUICK=${QUICK:-1} OUT=$OUTEXE nice -n 5 ./build.sh > $LOG 2>&1   # (-O1 test builds: the lead asked for QUICK=1)
  if [ $? -eq 0 ]; then echo "$(date +%T) built $OUTEXE after $i tries"; exit 0; fi
  ERR=$(grep -E "error|undefined|Killed" $LOG | head -6)
  if echo "$ERR" | grep -qE "$MINE"; then echo "MINE (build): $ERR"; exit 2; fi
  echo "$(date +%T) try $i build failed (foreign/OOM): $(echo "$ERR" | head -2)"; sleep 60
done
exit 1
