#!/bin/bash
# AI agent (set 20): after bld_s20a.sh, the test-only traced stair-pace exe nt_s20t.exe (AW/graft_s20tr.txt: nt_s20s's
# sources with ai.cpp = AW/s20_ai_trace.cpp, which LOGs who the cap held every 10 s). Restart-safe.
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
MAIN=/home/user/GTA-6-Claude-v0.5
until grep -q "bld_s20a done" $AW/bld_s20a.out 2>/dev/null; do sleep 30; done
if [ -f $MAIN/bin/nt_s20t.exe ] && grep -q "built bin/nt_s20t.exe" $AW/build_nt_s20t.out 2>/dev/null; then echo "have nt_s20t"; else
  bash $AW/ai_buildgraft2.sh bcb8a71 $AW/graft_s20tr.txt bin/nt_s20t.exe > $AW/build_nt_s20t.out 2>&1
  tail -1 $AW/build_nt_s20t.out
fi
echo "bld_s20b done $(date -u +%T)"
