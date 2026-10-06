#!/bin/bash
# far-LOD breakdown for batch 13 (measurement copy $S/dev20m with getenv toggles; not a candidate tree)
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab20m.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "start"
wmem 2000
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/dev20m citylab.cpp -o citylab_dev20m -lpthread > /tmp/city/clab_dev20m_build.log 2>&1
st "build rc=$?"
for V in none CL_NOFARCAP CL_NOFARPLANT CL_NOFARTANK CL_NOFARBALC ALL; do
  wmem 1500
  if [ $V = ALL ]; then CL_NOFARCAP=1 CL_NOFARPLANT=1 CL_NOFARTANK=1 CL_NOFARBALC=1 sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev20m /tmp/city/clab_dev20m_$V.txt
  elif [ $V = none ]; then sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev20m /tmp/city/clab_dev20m_$V.txt
  else env $V=1 sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev20m /tmp/city/clab_dev20m_$V.txt; fi
  st "perf $V done"
done
st "clab20m done"
