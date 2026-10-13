#!/bin/sh
# Build the last committed tree (HEAD, build-verified by the lead) with the current src/world on top, in a snapshot dir,
# so other agents' half-edited files never reach this build. usage: snapbuild.sh NAME [head] -> $SP/pl/snap_NAME/bin/places_agent.exe
# ("head": HEAD alone, the baseline for before shots)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
N=$1
D=$SP/pl/snap_$N
rm -rf $D; mkdir -p $D
cd /home/user/GTA-6-Claude-v0.5
git archive HEAD src tools build.sh | tar -x -C $D
if [ "$2" != "head" ]; then
  rm -rf $D/src/world
  cp -r src/world $D/src/world
fi
mkdir -p $D/build/gen $D/bin
[ -x build/embed_shaders ] && cp build/embed_shaders $D/build/
cd $D
sh $SP/memgate.sh 3000
QUICK=1 OUT=bin/places_agent.exe ./build.sh > $SP/pl/build_$N.log 2>&1
echo "exit $?" >> $SP/pl/build_$N.log
