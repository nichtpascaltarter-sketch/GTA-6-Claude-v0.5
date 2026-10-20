#!/bin/bash
# AI agent: nt_ai96 (c1af87a + the taxi fix + the exit-clip fix + the test app: HEAD's fixes for PR #46), then nt_ai97
# (c1af87a + the main tree's AI files: set 17 + both fixes + the knock mode) - one build at a time
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
echo "waiting for nt_ai96" > $AW/build_nt_ai97.out
bash $AW/ai_buildgraft.sh c1af87a $AW/graft96.txt bin/nt_ai96.exe > $AW/build_nt_ai96.out 2>&1
bash $AW/ai_buildgraft.sh c1af87a $AW/graft97.txt bin/nt_ai97.exe > $AW/build_nt_ai97.out 2>&1
