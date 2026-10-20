#!/bin/sh
# Build one exact tree (argument: tree hash) and, if it links, commit it on top of HEAD with the message in $2 (a file),
# then push. Other agents keep editing the working tree meanwhile; only this tree is committed.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
TREE=$1; MSGF=$2
D=/tmp/snapfixed
rm -rf $D && mkdir -p $D
cd $REPO && git archive $TREE | tar -x -C $D
cd $D && mkdir -p build/gen bin
$SP/memgate.sh 3200
if OUT=bin/snap.exe nice -n 5 ./build.sh > $SP/snap_fixed.log 2>&1; then
  # the build only proves the C++: every shader entry point must compile too (embedded HLSL is compiled at game start)
  $SP/memgate.sh 2400
  if ! sh $SP/shc_tree.sh $D > $SP/snap_shc.log 2>&1; then
    echo "SHADERS FAILED $TREE"; cat $SP/snap_shc.log; echo "BUILD FAILED (shaders) $TREE"; exit 1
  fi
  echo "$TREE" > $SP/snap_tree_ok
  cp bin/snap.exe $REPO/bin/nt_snap.exe
  cd $REPO && sh $SP/commit_snap.sh "$(cat $MSGF)" && git push -u origin claude/neon-tide 2>&1 | tail -1
  echo "COMMITTED $TREE"
else
  echo "BUILD FAILED $TREE"; grep -m5 -E "error|undefined" $SP/snap_fixed.log
fi
