#!/bin/sh
# Build the working tree with some directories/files taken from HEAD (to step around another agent's mid-edit).
# usage: lead_build.sh OUT.exe path1 [path2 ...]   (paths relative to the repo, e.g. src/ui)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
OUTEXE=$1; shift
D=/tmp/leadtree
rm -rf $D && mkdir -p $D
cd $REPO
export GIT_INDEX_FILE=$SP/lead_index
cp .git/index $GIT_INDEX_FILE
git add -A .
TREE=$(git write-tree)
unset GIT_INDEX_FILE
git archive $TREE | tar -x -C $D
for p in "$@"; do rm -rf $D/$p; git archive HEAD $p | tar -x -C $D; done
cd $D && mkdir -p build/gen bin
OUT=bin/out.exe nice -n 4 ./build.sh > $SP/lead_build.log 2>&1
rc=$?
[ $rc = 0 ] && cp bin/out.exe $OUTEXE
echo "build rc $rc"; grep -m5 -E "error|undefined" $SP/lead_build.log; exit $rc
