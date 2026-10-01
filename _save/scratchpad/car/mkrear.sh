#!/bin/sh
# builds the scratch tree's carview (scratchpad/rear) as car/cv_rear, gated on memory
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
sh $S/car/gate.sh sh -c "cd $S/rear && g++ -O2 -std=c++17 -I src tests/anim/carview.cpp -o $S/car/cv_rear.new 2>&1 | grep -E 'error|warning' | head -20" && mv $S/car/cv_rear.new $S/car/cv_rear && echo built
