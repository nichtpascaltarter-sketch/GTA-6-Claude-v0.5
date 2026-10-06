#!/bin/bash
# AI agent (one-off): once nt_ai67's build is through, freeze and build nt_ai68 (the follower slot keeping, the walk-by fix)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
until grep -qE "built bin/nt_ai67|MINE|foreign break" $AW/build_nt_ai67.out 2>/dev/null; do sleep 20; done
B=$(cat $AW/ai_buildloop.pid); while kill -0 $B 2>/dev/null; do sleep 5; done
echo "$(date +%T) nt_ai67: $(tail -1 $AW/build_nt_ai67.out)"
exec env HANDS=1 $AW/ai_buildtree.sh bin/nt_ai68.exe
