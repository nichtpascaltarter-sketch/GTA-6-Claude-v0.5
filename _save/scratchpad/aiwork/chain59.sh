#!/bin/bash
# the rest of nt_ai58's pedstop (its runner script was stopped: it used the lead's gate), then the nt_ai59 set
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
while kill -0 11619 2>/dev/null; do sleep 20; done
cp /tmp/wine_ai/drive_c/users/root/AppData/Local/NeonTide/log.txt $SP/log_pedstop_nt_ai58.txt 2>/dev/null
WINEPREFIX=/tmp/wine_ai /usr/lib/wine/wineserver64 -k 2>/dev/null
echo "pedstop (nt_ai58) done $(date +%T) shots=$(ls /tmp/ai/pedstop | wc -l)"
until grep -qE "built bin/nt_ai59|MINE" $AW/build_nt_ai59.out 2>/dev/null || ! kill -0 16487 2>/dev/null; do sleep 20; done
grep -q "built bin/nt_ai59" $AW/build_nt_ai59.out || { echo "nt_ai59 build: $(cat $AW/build_nt_ai59.out)"; exit 1; }
rm -f bin/nt_ai58.exe
echo "$(date +%T) the nt_ai59 set"
$AW/ai_runtests.sh bin/nt_ai59.exe arrest:260:12:60 events:240:12:60 takeover:110:12:60 pedstop:240:12:60 panic:40:12:3 life:200:12:100 brawl:150:12:60
