#!/bin/sh
# usage: bld_cl.sh <tree>  - QUICK (-O1) build of a clasp test tree via its build.sh -> bin/<tree>.exe
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
t=$1
cd $T/$t || exit 1
mkdir -p bin
for i in 1 2 3; do
  QUICK=1 OUT=bin/$t.exe nice -n 6 sh ./build.sh > $T/build_$t.log 2>&1
  rc=$?
  [ $rc -eq 0 ] && break
  grep -q "error" $T/build_$t.log && break
done
echo "build $t rc $rc"
tail -2 $T/build_$t.log
