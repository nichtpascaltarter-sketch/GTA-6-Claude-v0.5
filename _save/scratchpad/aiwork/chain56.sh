#!/bin/bash
# nt_ai52f (the sign-off set + fix): arrest; meanwhile build nt_ai56 (the working tree); then the nt_ai56 set
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
SF=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/snapfix
cd /home/user/GTA-6-Claude-v0.5
until grep -qE "built|FAILED" $AW/build_nt_ai52f.out 2>/dev/null; do sleep 15; done
grep -q FAILED $AW/build_nt_ai52f.out && { echo "nt_ai52f build failed"; exit 1; }
echo "$(date +%T) nt_ai52f ready; building nt_ai56"
$AW/ai_buildloop.sh bin/nt_ai56.exe > $AW/build_nt_ai56.out 2>&1 &
while pgrep -f "^/bin/bash [^ ]*ai_runtests.sh bin/nt_ai52[.]exe" >/dev/null; do sleep 20; done
echo "$(date +%T) arrest on nt_ai52f"
$AW/ai_runtests.sh $SF/bin/nt_ai52f.exe arrest:200:4:60
wait
grep -q "built bin/nt_ai56" $AW/build_nt_ai56.out || { echo "nt_ai56 build: $(cat $AW/build_nt_ai56.out)"; exit 1; }
echo "$(date +%T) the nt_ai56 set"
$AW/ai_runtests.sh bin/nt_ai56.exe pedstop:240:4:60 copbreak:160:4:60 panic:40:4:3 arrest:200:4:60 takeover:110:4:60 events:240:4:60 brawl:150:4:60 crowd:28.5:8:100
