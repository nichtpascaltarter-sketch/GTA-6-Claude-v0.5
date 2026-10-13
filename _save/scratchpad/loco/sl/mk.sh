#!/bin/sh
# usage: sl/mk.sh <tree-dir> <out>   locometer with the landing metrics (-DLM_LAND), gated on memory; out in sl/
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
sh $L/../car/gate.sh sh -c "g++ -O2 -std=c++17 -Wall -Wno-unused-function -DLM_LAND -I $1 $L/locometer.cpp -o $L/sl/$2.new 2>&1 | grep -E 'error|warning' | head -30" && [ -f $L/sl/$2.new ] && mv $L/sl/$2.new $L/sl/$2 && echo built $2
