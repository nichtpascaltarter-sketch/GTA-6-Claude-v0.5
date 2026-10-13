#!/bin/sh
# Mirror the working tree's src/ and tests/anim into $A/work; files named as arguments (half-edited by other agents)
# are taken from the last commit instead.
A=$(dirname "$0")
R=/home/user/GTA-6-Claude-v0.5
rm -rf $A/work
mkdir -p $A/work/tests/anim $A/work/tools
cp -r $R/src $A/work/src
cp $R/tests/anim/*.cpp $A/work/tests/anim/
cp $R/tools/native_stubs.cpp $A/work/tools/
for f in "$@"; do (cd $R && git show HEAD:$f) > $A/work/$f && echo "using HEAD $f"; done
