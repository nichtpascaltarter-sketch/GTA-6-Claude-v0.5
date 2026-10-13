#!/bin/bash
# after the nt_ai59 set (chain59): sets 3 and 4 together on nt_ai62 (once its build is done)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
cd /home/user/GTA-6-Claude-v0.5
while kill -0 16492 2>/dev/null; do sleep 20; done
until grep -qE "built bin/nt_ai62|MINE|foreign break" $AW/build_nt_ai62.out 2>/dev/null || ! kill -0 25105 2>/dev/null; do sleep 20; done
grep -q "built bin/nt_ai62" $AW/build_nt_ai62.out || { echo "nt_ai62 build: $(tail -3 $AW/build_nt_ai62.out)"; exit 1; }
rm -f bin/nt_ai59.exe bin/nt_ai60.exe bin/nt_ai61.exe
echo "$(date +%T) sets 3+4 on bin/nt_ai62.exe"
$AW/ai_runtests.sh bin/nt_ai62.exe ticket:150:12:60 proposal:150:12:60 crashscene:150:12:60 life:200:12:100 rain:90:12:100 hurt:150:12:30 arrest:260:12:60 pedstop:240:12:60 events:240:12:60 night:200:12:100
