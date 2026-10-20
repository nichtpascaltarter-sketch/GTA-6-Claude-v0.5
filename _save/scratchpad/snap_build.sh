#!/bin/sh
# Snapshot the whole working tree (tracked + untracked, not ignored) into a git tree object, build that exact tree in
# a separate directory, and record the tree hash when the build links. Commit afterwards with commit_snap.sh.
set -e
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
cd $REPO
export GIT_INDEX_FILE=$SP/snap_index
cp .git/index $GIT_INDEX_FILE
git add -A .
TREE=$(git write-tree)
unset GIT_INDEX_FILE
echo "tree $TREE"
D=/tmp/snapbuild
rm -rf $D && mkdir -p $D
git archive $TREE | tar -x -C $D
cp -r $REPO/build/embed_shaders $D/ 2>/dev/null || true
cd $D
if OUT=bin/snap.exe nice -n 5 ./build.sh > $SP/snap_build.log 2>&1; then
  echo "$TREE" > $SP/snap_tree_ok
  cp bin/snap.exe $REPO/bin/nt_snap.exe
  echo "BUILD OK $TREE"
else
  echo "BUILD FAILED"; grep -E "error|Error" $SP/snap_build.log | head -20
  exit 1
fi
