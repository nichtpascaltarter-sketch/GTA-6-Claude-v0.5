#!/bin/sh
# usage: mkviz.sh <tree-dir> <out>   builds lmviz against a tree (gated on memory)
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
sh $L/../car/gate.sh sh -c "g++ -O2 -std=c++17 -Wall -Wno-unused-function -Wno-unused-variable -I $1 $L/lmviz.cpp -o $2.new 2>&1 | grep -E 'error|warning' | head -30" && mv $2.new $2 && echo built $2
