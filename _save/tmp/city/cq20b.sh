#!/bin/bash
# dev20 after the far-LOD budget rework: host syntax check, citylab dev20 (triangle counts, clay views b13), then the
# MinGW syntax check and nt_dev20.exe, then the worldcheck (footprints) and the ASan worldcheck (no -g, memfree >= 4500).
# One compile at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cq20.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq20b start (cap-only far parapets)"
wmem 2000
cd $S/wc && nice -n 5 g++ -std=c++17 -fsyntax-only -I$S/dev20 worldcheck.cpp > /tmp/city/hostsyntax_dev20.log 2>&1
if grep -q "error" /tmp/city/hostsyntax_dev20.log; then st "dev20 host syntax errors"; exit 1; fi
wmem 2000
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/dev20 citylab.cpp -o citylab_dev20 -lpthread > /tmp/city/clab_dev20_build.log 2>&1
st "citylab build dev20c rc=$?"
wmem 1500
sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_dev20 /tmp/city/clab_dev20c_perf.txt; st "citylab perf dev20c done"
mkdir -p $S/city/clay_b13_dev20c
wmem 1500
cd /home/user/GTA-6-Claude-v0.5 && timeout 2400 nice -n 5 $S/citylab/citylab_dev20 --views $S/citylab/views_b13.txt $S/city/clay_b13_dev20c > /tmp/city/clab_views_b13_dev20c.txt 2>&1
st "clay views b13 dev20c exit $?"
st "cq20b paused for the triangle review"
