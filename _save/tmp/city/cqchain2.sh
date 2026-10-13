#!/bin/bash
# my compile queue, part 2 (after cqchain): dev18 (batch 13: roofs, balconies, bags on top of batch 12) syntax check and
# nt_dev18.exe; citylab on dev18: clay aerial views (views_b13) and their shots
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cqchain2.status; }
st "waiting for cqchain"
until grep -q "cqchain done" /tmp/city/cqchain.status 2>/dev/null; do sleep 20; done
cd $S/dev18 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/dev18 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev18.log 2>&1
if grep -q "error" /tmp/city/syntax_dev18.log; then st "dev18 syntax errors"; else
  st "dev18 syntax ok ($(grep -c warning /tmp/city/syntax_dev18.log) warnings)"
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
  rm -f /tmp/city/nt_dev18.exe
  cd $S/dev18 && { time OUT=/tmp/city/nt_dev18.exe sh ./build.sh ; } > /tmp/city/build_dev18.log 2>&1; st "dev18 build rc=$?"
  [ -f /tmp/city/nt_dev18.exe ] && st "dev18 exe ready" || st "no dev18 exe"
fi
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 10; done
cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/dev18 citylab.cpp -o citylab_dev18 -lpthread > /tmp/city/clab_dev18_build.log 2>&1
st "citylab build dev18 rc=$?"
for T in dev18 dev17; do
  B=$S/citylab/citylab_$T; [ -x $B ] || B=$S/citylab/citylab_dev15
  mkdir -p $S/city/clay_b13_$T
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1500 ]; do sleep 10; done
  cd /home/user/GTA-6-Claude-v0.5 && timeout 2400 nice -n 5 $B --views $S/citylab/views_b13.txt $S/city/clay_b13_$T > /tmp/city/clab_views_$T.txt 2>&1
  st "clay views $T exit $?"
done
st "cqchain2 done"
