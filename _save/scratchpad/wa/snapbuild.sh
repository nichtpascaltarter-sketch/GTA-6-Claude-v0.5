#!/bin/sh
# Snapshot the working tree (src, tools, build scripts) and build it there, so later edits don't leak into this build.
# usage: snapbuild.sh NAME  -> $SP/wa/snap_NAME/bin/world_agent.exe, log $SP/wa/build_NAME.log
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
N=$1
D=$SP/wa/snap_$N
rm -rf $D; mkdir -p $D
cd /home/user/GTA-6-Claude-v0.5
cp -r src tools build.sh $D/
mkdir -p $D/build/gen $D/bin
[ -x build/embed_shaders ] && cp build/embed_shaders $D/build/
cd $D
sh $SP/memgate.sh 3200
QUICK=1 OUT=bin/world_agent.exe ./build.sh > $SP/wa/build_$N.log 2>&1
echo "exit $?" >> $SP/wa/build_$N.log
