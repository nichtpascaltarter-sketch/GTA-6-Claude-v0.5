#!/bin/bash
# AI agent: nt_ai98 (= nt_ai96 + the cab's stop point: traffic_core.cpp) once the nt_ai96/97 chain is done - one build at a time
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
while kill -0 15559 2>/dev/null; do sleep 20; done
bash $AW/ai_buildgraft.sh c1af87a $AW/graft98.txt bin/nt_ai98.exe > $AW/build_nt_ai98.out 2>&1
