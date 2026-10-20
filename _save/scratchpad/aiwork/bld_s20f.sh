#!/bin/bash
# AI agent (set 20): the final game exes on HEAD 7b9146d: nt_s20f (the set 20 blobs, AW/graft_s20f.txt) and nt_s20ft (the
# same + the animation agent's test-only app.cpp c8bb416c with the locostairs mode), one after the other
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
AW=$SP/aiwork
bash $AW/ai_buildgraft2.sh 7b9146d $AW/graft_s20f.txt bin/nt_s20f.exe
echo "nt_s20f rc=$?"
bash $AW/ai_buildgraft2.sh 7b9146d $AW/graft_s20ft.txt bin/nt_s20ft.exe
echo "nt_s20ft rc=$?"
