#!/bin/sh
# bld45.sh (10-20 10:4x; replaces bld44, stopped at the start of its st21 build): locoplayer block v3g (the switchback's
# walkers one way at a time - v3e's jammed on the flights -, the chasers' spots) - st19 -> bin/loco_st19g.exe (c13b),
# st19a -> bin/loco_st19ag.exe (HEAD), st20 -> bin/loco_st20g.exe (c14d), st21 -> bin/loco_st21.exe (c15f; its first exe
# since v3d). One at a time, gated (memfree >= 2500, no fresh lead flag); <name>.building marks one being built (orch45
# waits for it). Restart-safe: an exe with v3g in it is skipped.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > "$T/bld45.pid"
gate() { while :; do if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then break; fi; sleep 30; done; }
echo "start $(date -u +%T)"
[ -f "$T/st21.building" ] && mv "$T/st21.building" "$T/st21.building.stale"
bldg() {   # bldg <tree> <exe> <name>: the tree's build into bin/<exe>, retried when killed (not on a compile error)
  if grep -q "one way at a time" "$T/$1/bin/$2" 2>/dev/null; then echo "have $2"; return 0; fi
  gate
  while ps -eo cmd | grep -v grep | grep -q "bin/$2"; do sleep 30; done
  touch "$T/$3.building"
  echo "build $1 -> $2 $(date -u +%T)"
  [ -f "$T/$1/bin/$2" ] && mv "$T/$1/bin/$2" "$T/$1/bin/${2%.exe}_pre45.exe"
  for i in 1 2 3; do
    (cd "$T/$1" && OUT=bin/$2 nice -n 6 sh ./build.sh) > "$T/build_$3.log" 2>&1
    r=$?
    [ $r -eq 0 ] && break
    grep -q "error" "$T/build_$3.log" && break
    gate
  done
  echo "build $1 -> $2 rc $r $(date -u +%T)"
  tail -2 "$T/build_$3.log"
  if [ ! -f "$T/$1/bin/$2" ]; then echo "$2 FAILED"; [ -f "$T/$1/bin/${2%.exe}_pre45.exe" ] && cp "$T/$1/bin/${2%.exe}_pre45.exe" "$T/$1/bin/$2"; fi
  mv "$T/$3.building" "$T/$3.built"
}
bldg st19 loco_st19g.exe st19g
bldg st19a loco_st19ag.exe st19ag
bldg st20 loco_st20g.exe st20g
bldg st21 loco_st21.exe st21
echo "bld45 done $(date -u +%T)"
mv "$T/bld45.pid" "$T/bld45.pid.done"
