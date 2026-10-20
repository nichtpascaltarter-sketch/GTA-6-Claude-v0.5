#!/bin/bash
# r.sh name args...   -> mel/name.png
cd /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
n=$1; shift
./preview mel/$n.ppm "$@" > mel/$n.log 2>&1 && convert mel/$n.ppm mel/$n.png && rm mel/$n.ppm
