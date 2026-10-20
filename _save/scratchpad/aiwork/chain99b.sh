#!/bin/bash
# AI agent: nt_ai99b (c1af87a + ef10180 + traffic_core 8efeb4a + ff4a3f8 + test app b1df55d) once the nt_ai96 rebuild is done
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
until grep -q "nt_ai96 build done" $AW/chain96b.log 2>/dev/null; do sleep 15; done
bash $AW/ai_buildgraft.sh c1af87a $AW/graft99b.txt bin/nt_ai99b.exe > $AW/build_nt_ai99b.out 2>&1
