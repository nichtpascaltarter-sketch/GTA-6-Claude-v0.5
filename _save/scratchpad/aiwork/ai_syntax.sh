#!/bin/bash
# AI agent: syntax-check the unity build of a tree (default: the main tree) - only with memfree >= 2000 MB (the lead's
# rule for host compiles); waits up to 10 min for the room, else gives up. Usage: ai_syntax.sh [TREE]
T=${1:-/home/user/GTA-6-Claude-v0.5}
for i in $(seq 1 60); do
  M=$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)
  [ "$M" -ge 2000 ] && break
  [ $i -eq 60 ] && { echo "no room (memfree $M)"; exit 3; }
  sleep 10
done
cd $T && nice -n 5 x86_64-w64-mingw32-g++ -std=c++17 -O0 -fsyntax-only --param ggc-min-expand=20 --param ggc-min-heapsize=32768 -march=x86-64-v2 -mfpmath=sse \
  -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen \
  src/main.cpp 2>&1 | grep -E "error|warning" | head -20
echo "syntax check done (memfree was $M)"
