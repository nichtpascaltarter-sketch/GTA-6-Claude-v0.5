#!/bin/bash
# light worldcheck runs (peak ~320 MB) with FP_NEAR on the before (fp_b) and after (fp_a) binaries
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
TAG=$1; shift
cd /home/user/GTA-6-Claude-v0.5
for b in fp_b fp_a; do
  while [ "$(sh tools/memfree.sh)" -lt 1200 ]; do sleep 5; done
  FP_ALLBOX=1 FP_NEAR=$1 nice -n 5 $S/wc/wc_$b 20 > $S/wc/near_${TAG}_$b.txt 2>&1
done
echo "near $TAG done $(date)" >> /tmp/city/wcfp.status
