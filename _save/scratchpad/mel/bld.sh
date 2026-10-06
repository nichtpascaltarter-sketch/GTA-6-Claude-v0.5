#!/bin/bash
# bld.sh [preview] [test] [parts] [mingw]  -- native tools into the scratchpad, mingw compile check of the anim module
cd /home/user/GTA-6-Claude-v0.5
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
for w in "$@"; do
  case $w in
    preview) nice -n 5 g++ -std=c++17 -O2 -I src tests/anim/preview.cpp -o $SP/preview 2>&1 | grep -E "error|warning" | head -20 ;;
    test) nice -n 5 g++ -std=c++17 -O2 -I src tests/anim/anim_test.cpp -o $SP/anim_test 2>&1 | grep -E "error|warning" | head -20 ;;
    parts) nice -n 5 g++ -std=c++17 -O2 -I src $SP/mel/parts.cpp -o $SP/mel/parts 2>&1 | grep -E "error|warning" | head -20 ;;
    mingw) nice -n 5 x86_64-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Wno-unused-function -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -c -I src -I build/gen -x c++ - -o /dev/null 2>&1 <<'XX' | grep -E "error|warning" | head -30
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
XX
    ;;
  esac
done
echo done
