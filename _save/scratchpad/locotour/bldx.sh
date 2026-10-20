#!/bin/sh
# bldx.sh <tree> <exe> <marker> <name> [<tree> <exe> <marker> <name>]...: test exes built one at a time, gated (memfree >=
# 2500, no fresh lead flag), each skipped when its exe holds the marker; <name>.building marks one being built (orch49
# waits for it); a killed compile is retried, a compile error is not. Log to stdout.
T=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/locotour
R=/home/user/GTA-6-Claude-v0.5
gate() { while :; do if [ -z "$(find /tmp/neontide_lead_wants -mmin -5 2>/dev/null)" ] && [ "$(sh $R/tools/memfree.sh 2>/dev/null || echo 0)" -ge 2500 ]; then break; fi; sleep 30; done; }
echo "start $(date -u +%T)"
while [ $# -ge 4 ]; do
  tr=$1; ex=$2; mk=$3; nm=$4; shift 4
  if grep -q "$mk" "$T/$tr/bin/$ex" 2>/dev/null; then echo "have $ex"; continue; fi
  gate
  touch "$T/$nm.building"
  echo "build $tr -> $ex $(date -u +%T)"
  for i in 1 2 3; do
    (cd "$T/$tr" && OUT=bin/$ex nice -n 6 sh ./build.sh) > "$T/build_$nm.log" 2>&1
    r=$?
    [ $r -eq 0 ] && break
    grep -q "error" "$T/build_$nm.log" && break
    gate
  done
  echo "build $tr -> $ex rc $r $(date -u +%T)"
  tail -2 "$T/build_$nm.log"
  mv "$T/$nm.building" "$T/$nm.built"
done
echo "bldx done $(date -u +%T)"
