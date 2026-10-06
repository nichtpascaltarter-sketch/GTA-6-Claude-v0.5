#!/bin/bash
# after the church renders: batch-3 clay views (the current sources), metrics, far-LOD breakdown, triangle counts
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
while [ ! -s /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/tasks/bq3kiqo6u.output ]; do sleep 20; done
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd /home/user/GTA-6-Claude-v0.5 && timeout 2400 nice -n 5 $S/citylab/citylab_dev3 --views $S/citylab/views_b3.txt $S/city/clay_b3 --stats --farbreak > /tmp/city/clab_b3_final.txt 2>&1
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
/tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev3 /tmp/city/clab_b3_perf.txt
echo "clab3q done" >> /tmp/city/clab3q.status
