#!/bin/sh
# usage: mkdbg.sh <name> : the debug harness sl/lm_<name>g from qs/ctree16 (its current animator) with the wall-guard prints
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
rm -rf $L/qs/ctree16g && cp -r $L/qs/ctree16 $L/qs/ctree16g && python3 $L/patches/dbgify.py $L/qs/ctree16g/src/anim/animator.cpp && cd $L && sh mk2.sh qs/ctree16g lm_$1g
