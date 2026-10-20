#!/bin/sh
# usage: sh apply.sh <tree>   writes worldv's slice 1+2 files into <tree> from their blobs (manifest.txt beside this)
M=$(dirname "$0")/manifest.txt
T=${1:?usage: apply.sh <tree>}
while read b p; do
  mkdir -p "$T/$(dirname "$p")" && git -C /home/user/GTA-6-Claude-v0.5 cat-file -p "$b" > "$T/$p" || exit 1
done < "$M"
echo "wrote $(wc -l < "$M") files into $T"
