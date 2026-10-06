#!/bin/bash
# my compile queue after the dev15 game build, one compile at a time: (1) worldcheck with the storefront side-wall shot
# planner, run; (2) dev17 (batch 12: storefronts kept to the street walls) syntax check and nt_dev17.exe; (3) citylab on
# dev16 (rooftops) and dev15, triangle counts
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cqchain.status; }
st "waiting for the dev15 build"
until grep -q "dev15 exe ready\|no dev15 exe" /tmp/city/cq15.status 2>/dev/null; do sleep 20; done
# (1)
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 10; done
cd $S/wc && nice -n 5 g++ -std=c++17 -O2 -I$S/dev15 worldcheck.cpp -o wc_sf -lpthread > /tmp/city/wc_sf_build.log 2>&1; st "wc_sf build rc=$?"
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1200 ]; do sleep 5; done
cd /home/user/GTA-6-Claude-v0.5 && FP_SFSHOTS=2 FP_BAGS="3168,-243;3350,-843;3093,1600;5079,1500;2607,4300" nice -n 5 $S/wc/wc_sf 20 > $S/wc/sf.txt 2>&1; st "wc_sf run exit $?"
# (2)
cd $S/dev17 && mkdir -p build/gen bin && g++ -O2 -std=c++17 tools/embed_shaders.cpp -o build/embed_shaders && ./build/embed_shaders src/shaders build/gen/shaders_embedded.h
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
cd $S/dev17 && nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/syntax_dev17.log 2>&1
if grep -q "error" /tmp/city/syntax_dev17.log; then st "dev17 syntax errors"; else
  st "dev17 syntax ok ($(grep -c warning /tmp/city/syntax_dev17.log) warnings)"
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2500 ]; do sleep 15; done
  rm -f /tmp/city/nt_dev17.exe
  cd $S/dev17 && { time OUT=/tmp/city/nt_dev17.exe sh ./build.sh ; } > /tmp/city/build_dev17.log 2>&1; st "dev17 build rc=$?"
  [ -f /tmp/city/nt_dev17.exe ] && st "dev17 exe ready" || st "no dev17 exe"
fi
# (3)
for T in dev16 dev15; do
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 2000 ]; do sleep 10; done
  cd $S/citylab && nice -n 5 g++ -std=c++17 -O2 -DCITYLAB_ARCH -DCITYLAB_YARDS -DCITYLAB_FRONTAGE -DCITYLAB_FORMS -I$S/$T citylab.cpp -o citylab_$T -lpthread > /tmp/city/clab_${T}_build.log 2>&1
  rc=$?; st "citylab build $T rc=$rc"; [ $rc -eq 0 ] || continue
  while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt 1500 ]; do sleep 10; done
  sh /tmp/city/tools/clab_perf.sh $S/citylab/citylab_$T /tmp/city/clab_${T}_perf.txt; st "citylab perf $T done"
done
st "cqchain done"
