#!/bin/bash
# after the dev14 rebuild: /tmp/city/nt_m10.exe = clean main b907098 (the stopgap and the shader fix together: the "before"
# for batch 10's storefront shots). One compile of mine at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq14.status; }
until grep -q "dev14b exe ready\|no dev14b exe\|dev14b syntax errors" /tmp/city/cq14.status 2>/dev/null; do sleep 20; done
rm -rf $S/m10 && mkdir -p $S/m10 && cd /home/user/GTA-6-Claude-v0.5 && git archive b907098 | tar -x -C $S/m10
cd $S/m10 && { time OUT=/tmp/city/nt_m10.exe sh ./build.sh ; } > /tmp/city/build_m10.log 2>&1; st "m10 build rc=$?"
[ -f /tmp/city/nt_m10.exe ] && st "m10 exe ready" || st "no m10 exe"
