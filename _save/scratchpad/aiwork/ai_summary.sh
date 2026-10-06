#!/bin/bash
# AI agent: the key lines of a test run. Usage: ai_summary.sh MODE TAG
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
F=$SP/wine_$1_$2.out
[ -f "$F" ] || { echo "no $F"; exit 1; }
echo "== $1 $2: $(grep -c . $F) lines"
grep -hE "autoplay $1:|police|events: |street meet|Unhandled|err:seh" $F | grep -vE "autoplay $1 t=" | sed 's/^\(.\{200\}\).*/\1/' | head -60
echo "-- last state"
grep -h "autoplay $1 t=" $F | tail -1 | sed 's/^\(.\{260\}\).*/\1/'
grep -h "autoplay $1 t=" $F | tail -1 | grep -o "totals.*" | sed 's/^\(.\{300\}\).*/\1/'
