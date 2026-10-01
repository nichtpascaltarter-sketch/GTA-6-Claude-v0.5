#!/bin/sh
# waitbuild.sh: wait until my storytest compile has started and finished, then report it
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
i=0
while ! pgrep -f "main_gen[.]cpp" > /dev/null && [ $i -lt 3600 ]; do sleep 10; i=$((i+10)); done
while pgrep -f "main_gen[.]cpp" > /dev/null; do sleep 15; done
sleep 5
ls -la --time-style=+%H:%M $SP/storytest/nt_storytest.exe 2>&1
grep -v "^In file included" $SP/storytest/build_full.log | grep -E "error|undefined" | head -8
