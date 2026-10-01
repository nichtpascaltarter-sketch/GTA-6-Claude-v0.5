#!/bin/bash
# after the nt_ai52 arrest (sign-off), the nt_ai55 set
AW=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/aiwork
cd /home/user/GTA-6-Claude-v0.5
while pgrep -f "^/bin/bash [^ ]*ai_runtests.sh bin/nt_ai52[.]exe" >/dev/null; do sleep 20; done
$AW/ai_runtests.sh bin/nt_ai55.exe pedstop:240:4:60 panic:40:4:3 arrest:200:4:60 takeover:110:4:60 events:240:4:60 brawl:150:4:60 crowd:28.5:8:100
