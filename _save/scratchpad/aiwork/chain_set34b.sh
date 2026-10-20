#!/bin/bash
# after the set 3+4 runs (chain_set34): the AI cost in a crowd and a traffic soak on nt_ai62
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
cd /home/user/GTA-6-Claude-v0.5
while kill -0 25214 2>/dev/null; do sleep 30; done
[ -f bin/nt_ai62.exe ] || { echo "no nt_ai62"; exit 1; }
echo "$(date +%T) crowd + soak on nt_ai62"
$AW/ai_runtests.sh bin/nt_ai62.exe crowd:28.5:12:100 soak:300:12:100
