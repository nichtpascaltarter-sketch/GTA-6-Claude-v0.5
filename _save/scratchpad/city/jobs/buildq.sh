#!/bin/sh
# my builds, one at a time: after the batch-1 exe, the matching baseline (same snapshot, HEAD world files)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while ! grep -q "^real" /tmp/city/build_b1.log 2>/dev/null; do sleep 20; done
cd $S/base2 && (time OUT=/tmp/city/nt_base2.exe sh ./build.sh) > /tmp/city/build_base2.log 2>&1
echo "buildq done $(date)" >> /tmp/city/buildq.status
