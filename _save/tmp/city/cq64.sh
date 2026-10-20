#!/bin/bash
# Campus path fix ($S/cp = snapshot 61 tree 21d9af33 + campus.cpp): MinGW syntax, the census against v0's (it must be
# unchanged), and the AI's --walkcheck all on base (s61) and fix (cp).
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
st() { echo "$1 $(date)" >> /tmp/city/cp.status; }
wmem() { while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt $1 ]; do sleep 15; done; }
st "cq64 start"
cd $S/cp
wmem 2000
nice -n 5 x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp > /tmp/city/cp_mingw.log 2>&1; st "MinGW main.cpp rc=$? errors $(grep -c 'error' /tmp/city/cp_mingw.log) warnings $(grep -c 'warning' /tmp/city/cp_mingw.log)"
wmem 2500
nice -n 5 g++ -O2 -std=c++17 -I. -I src $S/rf/rfcheck.cpp -o /tmp/city/rfcheck_cp -lpthread > /tmp/city/rfcheck_cp_build.log 2>&1; st "rfcheck_cp build rc=$?"
wmem 2000
nice -n 5 /tmp/city/rfcheck_cp > /tmp/city/census_cp.txt 2> /tmp/city/census_cp_err.txt; st "rfcheck_cp run rc=$?: record lines vs v0's: $(diff <(grep -E '^[A-Z] ' /tmp/city/census_cp.txt) <(grep -E '^[A-Z] ' /tmp/city/census_wm.txt) | grep -c '^[<>]') differ (of $(grep -cE '^[A-Z] ' /tmp/city/census_cp.txt)); all lines with times masked: $(diff <(sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/census_cp.txt) <(sed -E 's/[0-9]+\.[0-9]+ ?(s|ms)\b/T/g' /tmp/city/census_wm.txt) | grep -c '^[<>]') differ"
for V in s61 cp; do
  cd $S/$V
  wmem 2500
  nice -n 5 g++ -O2 -std=c++17 -I src tools/traffic_sim.cpp -o /tmp/city/ts_$V -lpthread > /tmp/city/ts_${V}_build.log 2>&1; st "ts_$V build rc=$?"
  wmem 2000
  nice -n 5 /tmp/city/ts_$V --walkcheck all > /tmp/city/walk_$V.txt 2>&1; st "ts_$V --walkcheck rc=$?: $(grep '^walk links through buildings' /tmp/city/walk_$V.txt); $(grep '^walk links on a carriageway' /tmp/city/walk_$V.txt); $(grep -c '^in building' /tmp/city/walk_$V.txt) in-building lines"
done
st "campus link: base $(grep -c 'through building 35588' /tmp/city/walk_s61.txt), fix $(grep -c 'through building 35588' /tmp/city/walk_cp.txt); in-building diff lines: $(diff <(grep '^in building' /tmp/city/walk_s61.txt | sed -E 's/walk link [0-9]+/walk link N/') <(grep '^in building' /tmp/city/walk_cp.txt | sed -E 's/walk link [0-9]+/walk link N/') | grep -c '^[<>]')"
st "cq64 done"
touch /tmp/city/cq64.done
