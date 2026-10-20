#!/bin/sh
# Symbolize the crash stack in a NeonTide log with addr2line against the -g build that produced it.
# Usage: symbolize.sh <exe-with-debug-info> <log.txt>
EXE=$1
LOG=$2
BASE=0x140000000
grep -E "offset 0x[0-9a-f]+" "$LOG" | grep -v "other module" | sed -E 's/.*offset (0x[0-9a-f]+).*/\1/' | while read off; do
  addr=$(printf "0x%x" $((BASE + off)))
  printf "%s  " "$off"
  x86_64-w64-mingw32-addr2line -f -C -i -e "$EXE" "$addr" | paste -sd ' ' | cut -c1-220
done
