#!/bin/bash
# my builds, one at a time: the batch-2 exe after the matching baseline
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while ! grep -q "^real" /tmp/city/build_base2.log 2>/dev/null; do sleep 20; done
cd $S/b2 && { time OUT=/tmp/city/nt_b2.exe sh ./build.sh ; } > /tmp/city/build_b2.log 2>&1
echo "buildq2 done $(date)" >> /tmp/city/buildq.status
