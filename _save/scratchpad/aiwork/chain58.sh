#!/bin/bash
# while nt_ai58 builds: takeover, events, crowd on nt_ai57 (the same code there); then the nt_ai58 set
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
cd /home/user/GTA-6-Claude-v0.5
$AW/ai_runtests.sh bin/nt_ai57.exe takeover:110:12:60 events:240:12:60 crowd:28.5:12:100
until grep -qE "built bin/nt_ai58|MINE" $AW/build_nt_ai58.out 2>/dev/null; do sleep 20; done
grep -q "built bin/nt_ai58" $AW/build_nt_ai58.out || { echo "nt_ai58 build: $(cat $AW/build_nt_ai58.out)"; exit 1; }
rm -f bin/nt_ai57.exe
echo "$(date +%T) the nt_ai58 set"
$AW/ai_runtests.sh bin/nt_ai58.exe arrest:260:12:60 pedstop:240:12:60 brawl:150:12:60 panic:40:12:3 life:200:12:100
