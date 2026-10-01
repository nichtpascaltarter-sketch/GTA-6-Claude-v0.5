#!/bin/sh
# usage: build.sh before|after   - the locotour exe of that tree (after: with -DLOCO_AFTER), via the tree's build.sh
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
t=$1
cd $T/$t || exit 1
mkdir -p bin
X=""
[ "$t" = after ] && X="-DLOCO_AFTER"
for i in 1 2 3; do
  OUT=bin/loco_$t.exe EXTRA="$X" nice -n 6 sh ./build.sh > $T/build_$t.log 2>&1
  rc=$?
  [ $rc -eq 0 ] && break
  grep -q "error" $T/build_$t.log && break   # (a compile error: no point retrying; a kill (137) is retried)
done
echo "build $t rc $rc"
tail -3 $T/build_$t.log
