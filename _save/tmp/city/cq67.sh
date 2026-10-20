#!/bin/bash
# The pilot's scratch tools: rbdump (world.bin's roads as text), ntwfmt and worldv from the rb tree. One build at a time.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/pilot.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
cd $S/rb
wmem 2000; nice -n 5 g++ -std=c++17 -O1 -Wall -Wextra -I. wmtools/rbdump.cpp -o /tmp/city/rbdump > /tmp/city/rbdump_build.log 2>&1; st "rbdump build rc=$?"
wmem 2000; nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/ntwfmt.cpp -o /tmp/city/ntwfmt > /tmp/city/ntwfmt_build.log 2>&1; st "ntwfmt build rc=$?"
wmem 2000; nice -n 5 g++ -std=c++17 -O2 -Wall -Wextra -I. tools/worldv.cpp -o /tmp/city/worldv > /tmp/city/worldv_build.log 2>&1; st "worldv build rc=$?"
touch /tmp/city/cq67_done.marker
