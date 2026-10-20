#!/bin/bash
# after the nt_ai59 set (chain59): set 3 on the newest AI build there is then (once a build of mine in progress is done)
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
cd /home/user/GTA-6-Claude-v0.5
while kill -0 16492 2>/dev/null; do sleep 20; done
while [ -f $AW/ai_buildloop.pid ] && kill -0 $(cat $AW/ai_buildloop.pid) 2>/dev/null; do sleep 20; done
EXE=$(ls -t bin/nt_ai6*.exe 2>/dev/null | head -1)
[ -n "$EXE" ] || { echo "no set-3 build"; exit 1; }
rm -f bin/nt_ai59.exe
for f in bin/nt_ai6*.exe; do [ "$f" != "$EXE" ] && rm -f "$f"; done
echo "$(date +%T) set 3 on $EXE"
$AW/ai_runtests.sh $EXE ticket:150:12:60 proposal:150:12:60 life:200:12:100 rain:90:12:100 arrest:260:12:60 pedstop:240:12:60 hurt:150:12:30 events:240:12:60
