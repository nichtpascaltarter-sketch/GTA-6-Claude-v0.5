#!/bin/bash
# AI agent: once the nt_ai101c build is over, build nt_ai102 (set 17 + change 2 + change 3 + the gotofollow / turntest
# modes: AW/graft102.txt on HEAD b71d339) - one build of mine at a time
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
P=$(cat $AW/build101c.pid)
while [ -d /proc/$P ] && grep -q "ai_buildgraft.sh" /proc/$P/cmdline 2>/dev/null; do sleep 20; done
echo "$(date +%T) nt_ai101c build over: $(tail -1 $AW/build_nt_ai101c.out)"
Q=$($AW/ai_detach.sh $AW/build_nt_ai102.out bash $AW/ai_buildgraft.sh b71d339 $AW/graft102.txt bin/nt_ai102.exe)
echo $Q > $AW/build102.pid
echo "$(date +%T) nt_ai102 build started (PID $Q)"
