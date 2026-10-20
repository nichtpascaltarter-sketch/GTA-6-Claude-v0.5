#!/bin/sh
# bld44.sh (10-20 10:0x; takes over from bld43 during its st20 build, which goes on): the chase test exes first, then st21
#  - st20: waits for bld43's build of it to end (its wrapper's PID in $T/bld44.st20pid), then marks it built as bld43 would
#  - a syntax check of block v3f (the chasers) in st19, then bin/loco_st19c.exe (c13b + v3f) and bin/loco_st19ac.exe
#    (HEAD + v3f); <tree>c.building marks one being built (orch44 waits for it)
#  - st20 / st21 (v3e) as bld43 builds them, when their exes lack v3e (st21; st20 too after a restart)
# one at a time, gated (memfree >= 2500, no fresh lead flag). Restart-safe: what is built is skipped.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > "$T/bld44.pid"
gate() { while :; do if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then break; fi; sleep 30; done; }
echo "start $(date -u +%T)"
P=$(cat "$T/bld44.st20pid" 2>/dev/null)
if [ -n "$P" ] && ps -o cmd= -p "$P" 2>/dev/null | grep -q "build.sh st20"; then
  while kill -0 "$P" 2>/dev/null; do sleep 20; done
  echo "st20 build ended $(date -u +%T)"
  if [ ! -f "$T/st20/bin/loco_st20.exe" ]; then echo "st20 FAILED"; [ -f "$T/st20/bin/loco_st20_pre43.exe" ] && cp "$T/st20/bin/loco_st20_pre43.exe" "$T/st20/bin/loco_st20.exe"; fi
  [ -f "$T/st20.building" ] && mv "$T/st20.building" "$T/st20.built"
  ls -la "$T/st20/bin/loco_st20.exe"
fi
[ -f "$T/bld44.st20pid" ] && mv "$T/bld44.st20pid" "$T/bld44.st20pid.done"
bldc() {   # bldc <tree> <exe>: the tree's build into bin/<exe>, retried when killed (not on a compile error)
  if grep -q "chasers:%s" "$T/$1/bin/$2" 2>/dev/null; then echo "have $2"; return 0; fi
  gate
  touch "$T/$1c.building"
  echo "build $1 -> $2 $(date -u +%T)"
  for i in 1 2 3; do
    (cd "$T/$1" && OUT=bin/$2 nice -n 6 sh ./build.sh) > "$T/build_$1c.log" 2>&1
    r=$?
    [ $r -eq 0 ] && break
    grep -q "error" "$T/build_$1c.log" && break
    gate
  done
  echo "build $1 -> $2 rc $r $(date -u +%T)"
  tail -2 "$T/build_$1c.log"
  mv "$T/$1c.building" "$T/$1c.built"
}
rc=0
if ! grep -q "chasers:%s" "$T/st19/bin/loco_st19c.exe" 2>/dev/null; then
  # the syntax check of v3f (c13b's tree)
  gate
  echo "syntax st19 $(date -u +%T)"
  (cd "$T/st19" && nice -n 6 x86_64-w64-mingw32-g++ -std=c++17 -DNDEBUG -fsyntax-only -march=x86-64-v2 -mfpmath=sse -fno-strict-aliasing -Wall -Wno-unused-function \
    -Wno-unused-variable -Wno-missing-braces -Wno-unused-but-set-variable -Wno-class-memaccess -Ibuild/gen src/main.cpp) > "$T/syntax_v3f.log" 2>&1
  rc=$?
  echo "syntax rc $rc $(date -u +%T)"
  grep -n "error" "$T/syntax_v3f.log" | head -5
fi
if [ $rc -eq 0 ]; then
  bldc st19 loco_st19c.exe
  bldc st19a loco_st19ac.exe
fi
# st20 / st21 (v3e), as bld43 builds them
for t in st20 st21; do
  if grep -q "8 treads of 17 cm" "$T/$t/bin/loco_$t.exe" 2>/dev/null; then echo "have $t"; continue; fi
  gate
  while ps -eo cmd | grep -v grep | grep -q "bin/loco_$t.exe"; do sleep 30; done
  touch "$T/$t.building"
  echo "build $t $(date -u +%T)"
  [ -f "$T/$t/bin/loco_$t.exe" ] && mv "$T/$t/bin/loco_$t.exe" "$T/$t/bin/loco_${t}_pre43.exe"
  sh "$T/build.sh" "$t"
  if [ ! -f "$T/$t/bin/loco_$t.exe" ]; then echo "$t FAILED"; [ -f "$T/$t/bin/loco_${t}_pre43.exe" ] && cp "$T/$t/bin/loco_${t}_pre43.exe" "$T/$t/bin/loco_$t.exe"; fi
  mv "$T/$t.building" "$T/$t.built"
done
echo "bld44 done $(date -u +%T)"
mv "$T/bld44.pid" "$T/bld44.pid.done"
