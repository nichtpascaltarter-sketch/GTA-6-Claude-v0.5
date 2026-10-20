#!/bin/sh
# MinGW compile check of the anim module with the Windows near/far macros defined (as windef.h does)
cd /home/user/GTA-6-Claude-v0.5
x86_64-w64-mingw32-g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-function -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -c -I src -I build/gen -x c++ - -o /dev/null 2>&1 <<'XX' | grep -E "error|warning" | head -30
#include <windows.h>
#include "core/math.cpp"
#include "render/mesh.cpp"
#include "anim/anim_all.cpp"
XX
echo wincheck done
