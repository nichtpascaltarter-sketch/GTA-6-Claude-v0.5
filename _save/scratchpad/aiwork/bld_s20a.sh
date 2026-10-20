#!/bin/bash
# AI agent (set 20): the stair pace A/B exes on bcb8a71 + set 19 + the anim c14 blobs + the anim agent's test app.cpp
# (locostairs): nt_s20s.exe (+ the stair pace cap, AW/graft_s20st.txt) then nt_s20b.exe (without, AW/graft_s20base.txt).
# Restart-safe: an exe already built is skipped. Relaunch: AW/ai_detach.sh AW/bld_s20a.out bash AW/bld_s20a.sh
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
MAIN=/home/user/GTA-6-Claude-v0.5
for t in "s20st nt_s20s" "s20base nt_s20b"; do
  set -- $t
  [ -f $MAIN/bin/$2.exe ] && grep -q "built bin/$2.exe" $AW/build_$2.out 2>/dev/null && { echo "have $2"; continue; }
  bash $AW/ai_buildgraft2.sh bcb8a71 $AW/graft_$1.txt bin/$2.exe > $AW/build_$2.out 2>&1
  tail -1 $AW/build_$2.out
done
echo "bld_s20a done $(date -u +%T)"
