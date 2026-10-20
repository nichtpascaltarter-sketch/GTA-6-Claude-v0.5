#!/bin/sh
# kitrun.sh <bin> <tag>: the kit sweep at 0.5 m/s and at the walkers' pace (60 Hz, turn profile 2) into r11/kit_<tag>_{iv5,pace}.txt
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
cd $L
LM_KIT=1 LM_IV=0.5 LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/$1 kit. > r11/kit_$2_iv5.txt 2>/dev/null
LM_KIT=1 LM_HZ=60 LM_TURNPROF=2 nice -n 4 ./sl/$1 kit. > r11/kit_$2_pace.txt 2>/dev/null
