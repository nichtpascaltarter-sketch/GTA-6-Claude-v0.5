#!/bin/bash
# AI agent: a graft list of the main tree's AI files that differ from COMMIT (written into the object store with -w):
# "BLOB src/game/FILE" lines - only my files, never others' work in progress. Usage: mkgraft.sh COMMIT OUTLIST
MAIN=/home/user/GTA-6-Claude-v0.5
COMMIT=$1
OUT=$2
: > $OUT
for f in ai.cpp ai_core.h ai_game.h app.cpp barks.cpp events.cpp pedai.cpp pednav.cpp police.cpp traffic.cpp traffic_core.cpp lanes.cpp wildlife.cpp wildlife.h; do
  B=$(git -C $MAIN hash-object -w $MAIN/src/game/$f)
  H=$(git -C $MAIN rev-parse $COMMIT:src/game/$f)
  [ "$B" != "$H" ] && echo "$B src/game/$f" >> $OUT
done
cat $OUT
