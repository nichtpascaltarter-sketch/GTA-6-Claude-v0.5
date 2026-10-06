#!/bin/bash
# build nt_ai57 (the working tree), then its test set (render every 12th frame: the machine is busy)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
cd /home/user/GTA-6-Claude-v0.5
$AW/ai_buildloop.sh bin/nt_ai57.exe > $AW/build_nt_ai57.out 2>&1
grep -q "built bin/nt_ai57" $AW/build_nt_ai57.out || { echo "nt_ai57 build: $(cat $AW/build_nt_ai57.out)"; exit 1; }
rm -f bin/nt_ai56.exe
echo "$(date +%T) the nt_ai57 set"
$AW/ai_runtests.sh bin/nt_ai57.exe pedstop:240:12:60 copbreak:160:12:60 panic:40:12:3 arrest:260:12:60 takeover:110:12:60 events:240:12:60 brawl:150:12:60 crowd:28.5:12:100
