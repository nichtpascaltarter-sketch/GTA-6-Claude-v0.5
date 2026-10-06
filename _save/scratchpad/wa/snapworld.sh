#!/bin/sh
# Build a snapshot from an earlier good snapshot's tree with the current src/world on top (other agents' half-edited files
# stay as they were in BASE). usage: snapworld.sh BASE NAME -> $SP/wa/snap_NAME/bin/world_agent.exe, log $SP/wa/build_NAME.log
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
B=$SP/wa/snap_$1
D=$SP/wa/snap_$2
rm -rf $D; mkdir -p $D
cp -r $B/src $B/tools $B/build.sh $D/
rm -rf $D/src/world
cp -r /home/user/GTA-6-Claude-v0.5/src/world $D/src/world
mkdir -p $D/build/gen $D/bin
[ -x $B/build/embed_shaders ] && cp $B/build/embed_shaders $D/build/
cd $D
sh $SP/memgate.sh 3200
QUICK=1 OUT=bin/world_agent.exe ./build.sh > $SP/wa/build_$2.log 2>&1
echo "exit $?" >> $SP/wa/build_$2.log
