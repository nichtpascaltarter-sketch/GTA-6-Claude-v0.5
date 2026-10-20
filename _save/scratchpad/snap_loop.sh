#!/bin/sh
# Repeatedly snapshot the working tree until a snapshot passes a syntax check and then a full build+link.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
TRIES=${TRIES:-12}
i=0
while [ $i -lt $TRIES ]; do
  i=$((i+1))
  cd $REPO
  export GIT_INDEX_FILE=$SP/snap_index
  cp .git/index $GIT_INDEX_FILE
  git add -A .
  TREE=$(git write-tree)
  unset GIT_INDEX_FILE
  D=/tmp/snapbuild
  rm -rf $D && mkdir -p $D
  git archive $TREE | tar -x -C $D
  cd $D
  mkdir -p build/gen bin
  g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h > /dev/null
  $SP/memgate.sh 2200
  if ! nice -n 5 x86_64-w64-mingw32-g++ -std=c++17 -fsyntax-only --param ggc-min-expand=20 --param ggc-min-heapsize=32768 -Ibuild/gen src/main.cpp > $SP/snap_syntax.log 2>&1; then
    echo "try $i: syntax errors: $(grep -m3 'error' $SP/snap_syntax.log | tr '\n' ' ' | cut -c1-300)"
    sleep 150
    continue
  fi
  $SP/memgate.sh 2600
  if OUT=bin/snap.exe nice -n 5 ./build.sh > $SP/snap_build.log 2>&1; then
    echo "$TREE" > $SP/snap_tree_ok
    cp bin/snap.exe $REPO/bin/nt_snap.exe
    echo "BUILD OK $TREE (try $i)"
    exit 0
  fi
  echo "try $i: build failed: $(grep -m3 -E 'error|undefined' $SP/snap_build.log | tr '\n' ' ' | cut -c1-300)"
  sleep 120
done
echo "GAVE UP"
exit 1
