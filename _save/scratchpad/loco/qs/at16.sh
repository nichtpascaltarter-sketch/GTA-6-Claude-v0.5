#!/bin/sh
# at16.sh <tag> [animator.cpp character.h]: anim_test on attree16 (HEAD 7b9146d; with the given files when passed, put
# back after) -> sl/at_<tag>.log
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
A=$L/attree16
cp $A/src/anim/animator.cpp $L/attree16_animator.orig; cp $A/src/anim/character.h $L/attree16_character.orig
[ -n "$2" ] && cp "$2" $A/src/anim/animator.cpp && cp "$3" $A/src/anim/character.h
for i in 1 2 3 4 5 6; do
  sh $L/cgate.sh sh -c "cd $A && g++ -O2 -std=c++17 -I . -I src tests/anim/anim_test.cpp -o $L/sl/at_$1.new 2>&1 | grep -E 'error|warning' | head -20" && [ -f $L/sl/at_$1.new ] && mv $L/sl/at_$1.new $L/sl/at_$1 && break
done
cd $A && $L/sl/at_$1 > $L/sl/at_$1.log 2>&1
echo "anim_test $1 rc $?"
tail -1 $L/sl/at_$1.log
mv $L/sl/at_$1 $L/sl/at_$1.bin.old
cp $L/attree16_animator.orig $A/src/anim/animator.cpp; cp $L/attree16_character.orig $A/src/anim/character.h
