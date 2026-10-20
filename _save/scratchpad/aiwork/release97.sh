#!/bin/bash
# AI agent: once nt_ai99b is built (or failed), put the held set 17 runs (nt_ai97) back at the end of the queue
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
until grep -qE "built bin/nt_ai99b|MINE|foreign" $AW/build_nt_ai99b.out 2>/dev/null; do sleep 20; done
sleep 60   # (the runner takes nt_ai99b's taxi first)
while IFS= read -r l; do [ -n "$l" ] && flock $AW/queue.lock python3 $AW/qedit.py add "$l"; done < $AW/queue_held97.txt
echo "$(date +%T) released the nt_ai97 runs"
