#!/bin/sh
# bld47.sh (10-20 11:2x): after bld46, st21 -> bin/loco_st21i.exe (c15f + block v3i: the spiral's walker on the walking
# line from the start). Gated (memfree >= 2500, no fresh lead flag); st21i.building marks it being built. Restart-safe.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
echo $$ > "$T/bld47.pid"
gate() { while :; do if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then break; fi; sleep 30; done; }
echo "start $(date -u +%T)"
while [ -f "$T/bld46.pid" ] && kill -0 "$(cat $T/bld46.pid)" 2>/dev/null; do sleep 20; done
if grep -q "on the walking line" "$T/st21/bin/loco_st21i.exe" 2>/dev/null; then echo "have loco_st21i.exe"
else
  gate
  touch "$T/st21i.building"
  echo "build st21 -> loco_st21i.exe $(date -u +%T)"
  for i in 1 2 3; do
    (cd "$T/st21" && OUT=bin/loco_st21i.exe nice -n 6 sh ./build.sh) > "$T/build_st21i.log" 2>&1
    r=$?
    [ $r -eq 0 ] && break
    grep -q "error" "$T/build_st21i.log" && break
    gate
  done
  echo "build st21 -> loco_st21i.exe rc $r $(date -u +%T)"
  tail -2 "$T/build_st21i.log"
  mv "$T/st21i.building" "$T/st21i.built"
fi
echo "bld47 done $(date -u +%T)"
mv "$T/bld47.pid" "$T/bld47.pid.done"
