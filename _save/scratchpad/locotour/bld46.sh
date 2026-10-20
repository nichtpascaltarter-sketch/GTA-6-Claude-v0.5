#!/bin/sh
# bld46.sh (10-20 11:2x): locoplayer block v3h (the switchback walkers' turn as a half circle: v3g's two right angles had
# the AI hold them up at the landing) - st19 -> bin/loco_st19h.exe (c13b), st20 -> bin/loco_st20h.exe (c14d). One at a
# time, gated (memfree >= 2500, no fresh lead flag); <name>.building marks one being built (orch46 waits). Restart-safe.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > "$T/bld46.pid"
gate() { while :; do if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then break; fi; sleep 30; done; }
echo "start $(date -u +%T)"
for tb in "st19 loco_st19h.exe st19h" "st20 loco_st20h.exe st20h"; do
  set -- $tb
  if grep -q "a half circle round" "$T/$1/bin/$2" 2>/dev/null; then echo "have $2"; continue; fi
  gate
  touch "$T/$3.building"
  echo "build $1 -> $2 $(date -u +%T)"
  for i in 1 2 3; do
    (cd "$T/$1" && OUT=bin/$2 nice -n 6 sh ./build.sh) > "$T/build_$3.log" 2>&1
    r=$?
    [ $r -eq 0 ] && break
    grep -q "error" "$T/build_$3.log" && break
    gate
  done
  echo "build $1 -> $2 rc $r $(date -u +%T)"
  tail -2 "$T/build_$3.log"
  mv "$T/$3.building" "$T/$3.built"
done
echo "bld46 done $(date -u +%T)"
mv "$T/bld46.pid" "$T/bld46.pid.done"
