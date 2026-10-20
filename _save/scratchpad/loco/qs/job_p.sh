#!/bin/sh
# job_p.sh: anim_test for the final c14d (attree15) and c15f (attree16) blobs -> sl/at_qs14d.log, sl/at_qs15f.log
L=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/loco
at() {   # at <tree> <tag>
  for i in 1 2 3 4 5 6; do
    sh $L/cgate.sh sh -c "cd $L/$1 && g++ -O2 -std=c++17 -I . -I src tests/anim/anim_test.cpp -o $L/sl/at_$2.new 2>&1 | grep -E 'error|warning' | head -20" && [ -f $L/sl/at_$2.new ] && mv $L/sl/at_$2.new $L/sl/at_$2 && break
  done
  (cd $L/$1 && $L/sl/at_$2 > $L/sl/at_$2.log 2>&1); echo "anim_test $2 rc $? $(tail -n 1 $L/sl/at_$2.log)"
  rm -f $L/sl/at_$2
}
cp $L/qs/clean14d/animator_qs14d.cpp $L/attree15/src/anim/animator.cpp
cp $L/qs/clean14d/character_qs14d.h $L/attree15/src/anim/character.h
at attree15 qs14d
cp $L/qs/clean15f/animator_qs15f.cpp $L/attree16/src/anim/animator.cpp
cp $L/qs/clean15f/character_qs15f.h $L/attree16/src/anim/character.h
at attree16 qs15f
echo "job_p done"
