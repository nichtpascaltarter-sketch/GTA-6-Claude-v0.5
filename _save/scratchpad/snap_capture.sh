#!/bin/sh
# Capture the working tree as a snapshot tree (temp index), holding back the paths listed in snap_exclude.txt (they
# keep HEAD's version). Prints the tree hash. usage: snap_capture.sh NAME   (index copy: $SP/NAME_index)
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5 || exit 2
IDX=$SP/${1:-snap}_index
cp .git/index "$IDX"
GIT_INDEX_FILE="$IDX" git add -A || exit 1
grep -v '^#' $SP/snap_exclude.txt | grep -v '^[[:space:]]*$' | while read -r p; do
  GIT_INDEX_FILE="$IDX" git reset -q HEAD -- "$p" 2>/dev/null
done
GIT_INDEX_FILE="$IDX" git write-tree
