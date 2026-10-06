#!/bin/bash
# after sets 3+4 (chain_set34 / chain_set34b on nt_ai62): set 5 on nt_ai64 (once its build is done)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
cd /home/user/GTA-6-Claude-v0.5
while kill -0 25214 2>/dev/null || kill -0 25290 2>/dev/null; do sleep 30; done
until grep -qE "built bin/nt_ai64|MINE|foreign break" $AW/build_nt_ai64.out 2>/dev/null; do sleep 30; done
grep -q "built bin/nt_ai64" $AW/build_nt_ai64.out || { echo "nt_ai64 build: $(tail -3 $AW/build_nt_ai64.out)"; exit 1; }
rm -f bin/nt_ai62.exe bin/nt_ai63.exe
echo "$(date +%T) set 5 on bin/nt_ai64.exe"
$AW/ai_runtests.sh bin/nt_ai64.exe panhandle:110:12:60 busrun:170:12:60 jaywalk:120:12:60 crowded:90:12:60 followed:100:12:60 nearmiss:120:12:30 rain:90:12:100 crashscene:150:12:60 life:200:12:100 soak:300:12:100
