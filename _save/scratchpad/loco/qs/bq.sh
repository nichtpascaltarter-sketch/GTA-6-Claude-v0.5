#!/bin/sh
# builds qs/lm_qs from the gated tree (qs/tree), with the landing metrics, gated on memory
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
sh $L/../car/gate.sh sh -c "g++ -O2 -std=c++17 -Wall -Wno-unused-function -DLM_LAND -I $L/qs/tree $L/locometer.cpp -o $L/qs/lm_qs.new 2>&1 | grep -E 'error|warning' | head -30" && [ -f $L/qs/lm_qs.new ] && mv $L/qs/lm_qs.new $L/qs/lm_qs && echo built lm_qs
