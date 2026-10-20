#!/bin/bash
# AI agent: syntax-check exactly a graft - a commit's tree (git archive) with files replaced by the blobs in a list ("BLOB path"
# lines) and the AI peds.cpp blocks pasted where missing (ai_hands.py) - with the build's mingw flags; memfree >= 2000 first.
# Usage: ai_syntaxgraft.sh COMMIT BLOBLIST TAG
MAIN=/home/user/GTA-6-Claude-v0.5
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
COMMIT=$1; LIST=$2; TAG=$3
T=$SP/tree_syn_$TAG
rm -rf $T; mkdir -p $T/build
git -C $MAIN archive $COMMIT src tools build.sh | tar -x -C $T || exit 5
mkdir -p $T/build/gen && cp -a $MAIN/build/embed_shaders $T/build/ && (cd $T && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h > /dev/null) || exit 7
while read -r B F; do
  [ -z "$B" ] && continue
  git -C $MAIN cat-file -p $B > $T/$F || exit 6
done < $LIST
python3 $AW/ai_hands.py $T/src/game/peds.cpp || exit 4
$AW/ai_syntax.sh $T
cd $SP && rm -rf $T
