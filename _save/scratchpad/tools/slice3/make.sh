#!/bin/sh
# Writes worldv's files as blobs into the shared repo's object store and lists them (manifest.txt): the files that
# slice 3 adds or changes against slice 1+2 (in snapshot 60), each with its slice 1+2 blob when it had one.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
T=/tmp/tools/tree
R=/home/user/GTA-6-Claude-v0.5
OUT=$SP/tools/slice3
OLD=$SP/tools/slice12/manifest.txt
cd $T || exit 1
{ echo src/world/validate.h; echo src/world/validate.cpp; echo src/world/validate_layout.h; echo src/world/validate_layout.cpp
  echo tools/worldv.cpp; echo tests/world/worldv_test.cpp; find tests/world/fixtures -type f -name "*.ntw" | sort; } > $OUT/paths.txt
: > $OUT/manifest.txt
: > $OUT/changed.txt
while read p; do
  b=$(git -C $R hash-object -w "$T/$p") || exit 1
  old=$(awk -v p="$p" '$2 == p {print $1}' $OLD)
  echo "$b $p" >> $OUT/manifest.txt
  if [ "$b" != "$old" ]; then echo "$b ${old:-new} $p" >> $OUT/changed.txt; fi
done < $OUT/paths.txt
# files of slice 1+2 that slice 3 drops (none expected)
awk '{print $2}' $OLD | while read p; do grep -q " $p\$" $OUT/manifest.txt || echo "DROPPED $p"; done
wc -l < $OUT/manifest.txt; wc -l < $OUT/changed.txt
