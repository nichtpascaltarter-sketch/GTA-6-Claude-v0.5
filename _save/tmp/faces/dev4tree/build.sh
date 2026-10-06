#!/bin/sh
# Cross-compiles NeonTide.exe on Linux with MinGW-w64 (static, no DLL dependencies).
# Usage: ./build.sh [debug]   (OUT=path/to.exe overrides the output file, EXTRA="-DNO_CHARACTERS" adds flags)
set -e
cd "$(dirname "$0")"
mkdir -p build/gen bin
CXX=${CXX:-x86_64-w64-mingw32-g++}
if [ ! -x build/embed_shaders ] || [ tools/embed_shaders.cpp -nt build/embed_shaders ]; then
  g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders
fi
./build/embed_shaders src/shaders build/gen/shaders_embedded.h
OPT="-O2 -DNDEBUG"
# Keep the unity build's peak memory near 1.2 GB (several builds may run at once on small machines)
GCCMEM="--param ggc-min-expand=20 --param ggc-min-heapsize=32768"
STRIP="-s"
if [ "$1" = "debug" ]; then OPT="-O0 -g"; STRIP=""; fi
# QUICK=1: -O1 for iteration builds on a busy test machine (about a third less compile time; not for timings)
if [ "${QUICK:-0}" = 1 ] && [ "$1" != "debug" ]; then OPT="-O1 -DNDEBUG"; fi
# Machines that run several builds at once (the automated test rig) serialize them: each compile peaks near 2.1 GB.
# Opt in by creating /tmp/neontide_build.lock; the lock is released when this script exits.
if [ -e /tmp/neontide_build.lock ] && command -v flock >/dev/null 2>&1; then
  exec 9>>/tmp/neontide_build.lock
  flock 9
  # the wait for the lock can be long: start only once there is room for the compile (Wine runs come and go; the
  # room is counted under the container's memory cap, where hitting it kills the compile)
  if [ -r /proc/meminfo ]; then
    # (under the launch lock games take too, held 15 s past the check until the compile shows up in memfree's count)
    exec 7>>/tmp/neontide_launch.lock
    flock 7
    n=0
    while [ "$(sh tools/memfree.sh 2>/dev/null || echo 100000)" -lt 3000 ] && [ $n -lt 90 ]; do sleep 10; n=$((n+1)); done
    sleep 15 </dev/null >/dev/null 2>&1 9>&- &
    exec 7>&-
  fi
fi
$CXX -std=c++17 $OPT $EXTRA $GCCMEM -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function -Wno-unused-variable \
  -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen \
  src/main.cpp -o ${OUT:-bin/NeonTide.exe} $STRIP -static -static-libgcc -static-libstdc++ -mwindows \
  -ldxgi -luuid -lole32 -loleaut32 -lwinmm -lshell32 -luser32 -lgdi32 -lkernel32 -lavrt
ls -la ${OUT:-bin/NeonTide.exe}
