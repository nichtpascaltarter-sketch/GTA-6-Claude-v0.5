#!/bin/bash
# the far HVAC unit's share of the far ring (measurement copy $S/dev20m: getenv toggles; throwaway), after cq22
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/clab22m.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "waiting for cq22"
until grep -q "cq22 done" /tmp/city/cq22.status 2>/dev/null; do sleep 30; done
wmem 2000
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/dev20m citylab.cpp -o citylab_dev20m -lpthread > /tmp/city/clab_dev20m_build.log 2>&1
st "build rc=$?"
for V in none CL_NOFARHVAC CL_NOFARPLANT; do
  wmem 1200
  if [ $V = none ]; then sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev20m /tmp/city/clab_dev20m2_$V.txt
  else env $V=1 sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev20m /tmp/city/clab_dev20m2_$V.txt; fi
  st "perf $V done"
done
rm -f $S/citylab/citylab_dev20m; rm -rf $S/dev20m
st "clab22m done"
