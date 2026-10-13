#!/bin/sh
# Syntax-only check of the whole unity build (src/main.cpp).
cd /home/user/GTA-6-Claude-v0.5
x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces \
  -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen -Isrc src/main.cpp 2>&1 | grep -v "^In file included" | head -${1:-60}
