#!/bin/bash
# Guarded graft of a merged range into the shared work tree: a file still at the OLD commit's version (or absent in
# both) gets the NEW version; one already at NEW is left; anything else is an agent's work in progress and is kept.
# usage: graft.sh OLD NEW
cd /home/user/GTA-6-Claude-v0.5 || exit 2
OLD=$1; NEW=$2; up=0; same=0; kept=0
for p in $(git diff --name-only $OLD $NEW); do
  o=$(git rev-parse -q --verify "$OLD:$p" 2>/dev/null); n=$(git rev-parse -q --verify "$NEW:$p" 2>/dev/null)
  if [ -e "$p" ]; then w=$(git hash-object "$p"); else w=; fi
  if [ "$w" = "$n" ]; then same=$((same+1)); continue; fi
  if [ "$w" = "$o" ]; then
    if [ -n "$n" ]; then mkdir -p "$(dirname "$p")"; git cat-file -p "$n" > "$p"; m=$(git ls-tree $NEW -- "$p" | awk '{print $1}'); [ "$m" = 100755 ] && chmod +x "$p"; else rm -f "$p"; fi
    up=$((up+1))
  else
    kept=$((kept+1)); echo "kept (agent WIP): $p"
  fi
done
echo "grafted $up, already $same, kept $kept"
