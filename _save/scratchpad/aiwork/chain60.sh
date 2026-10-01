#!/bin/bash
# after the nt_ai59 set (chain59) and the nt_ai60 build: the nt_ai60 set (set 3: tickets, hands, greetings, walk-round)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
cd /home/user/GTA-6-Claude-v0.5
while kill -0 16492 2>/dev/null; do sleep 20; done
until grep -qE "built bin/nt_ai60|MINE|foreign" $AW/build_nt_ai60.out 2>/dev/null || ! kill -0 26285 2>/dev/null; do sleep 20; done
grep -q "built bin/nt_ai60" $AW/build_nt_ai60.out || { echo "nt_ai60 build: $(cat $AW/build_nt_ai60.out)"; exit 1; }
rm -f bin/nt_ai59.exe
echo "$(date +%T) the nt_ai60 set"
$AW/ai_runtests.sh bin/nt_ai60.exe ticket:150:12:60 proposal:150:12:60 life:200:12:100 pedstop:240:12:60 hurt:150:12:30 events:240:12:60
