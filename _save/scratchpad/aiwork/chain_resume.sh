#!/bin/bash
# after the reboot: sets 3+4 on nt_ai62 (the couple hands and arrest stage 3 first), then set 5 on nt_ai64 once its build
# is done - one script, so nothing waits on another process's PID
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
cd /home/user/GTA-6-Claude-v0.5
echo "$(date +%T) sets 3+4 on bin/nt_ai62.exe"
$AW/ai_runtests.sh bin/nt_ai62.exe life:200:12:100 arrest:260:12:60 ticket:150:12:60 proposal:150:12:60 crashscene:150:12:60 rain:90:12:100 hurt:150:12:30 pedstop:240:12:60 events:240:12:60 night:200:12:100 crowd:28.5:12:100 soak:300:12:100
until grep -qE "built bin/nt_ai64|MINE|foreign break" $AW/build_nt_ai64.out 2>/dev/null; do sleep 30; done
grep -q "built bin/nt_ai64" $AW/build_nt_ai64.out || { echo "nt_ai64 build: $(tail -3 $AW/build_nt_ai64.out)"; exit 1; }
echo "$(date +%T) set 5 on bin/nt_ai64.exe"
$AW/ai_runtests.sh bin/nt_ai64.exe panhandle:110:12:60 busrun:170:12:60 jaywalk:120:12:60 crowded:90:12:60 followed:100:12:60 nearmiss:120:12:30 rain:90:12:100 crashscene:150:12:60 life:200:12:100 soak:300:12:100
echo "$(date +%T) chain done"
