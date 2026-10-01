#!/bin/sh
# Test-only build of the game with a scratch copy of main.cpp (adds world/sites*.cpp when main.cpp lacks them).
set -e
cd /home/user/GTA-6-Claude-v0.5
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
if grep -q "sites.cpp" src/main.cpp; then cp src/main.cpp $SP/main_ui.cpp; else
  sed 's#^\#include "world/cellgen.cpp"#\#include "world/sites.cpp"\n\#include "world/sitegeo.cpp"\n\#include "world/cellgen.cpp"#' src/main.cpp > $SP/main_ui.cpp; fi
x86_64-w64-mingw32-g++ -std=c++17 -O2 -DNDEBUG $EXTRA --param ggc-min-expand=20 --param ggc-min-heapsize=32768 -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable \
  -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen -Isrc \
  $SP/main_ui.cpp -o ${OUT:-bin/nt_ui.exe} -s -static -static-libgcc -static-libstdc++ -mwindows \
  -ld3d11 -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt
ls -la ${OUT:-bin/nt_ui.exe}
