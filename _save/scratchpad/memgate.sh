#!/bin/sh
# Wait until enough memory is free before a heavy step. usage: memgate.sh [MB]. The free room is tools/memfree.sh's.
# Called from the lead's snapshot checks (snap_smoke.sh / snap_resume_*.sh) it also keeps /tmp/neontide_lead_wants
# fresh while waiting, so the agents' run.sh / build.sh hold back new games and builds until the lead has room (they
# ignore a flag older than 5 minutes), and leaves it 60 s past the gate so the lead's own launch is counted first.
# Called from anywhere else it is a plain wait and raises no flag.
NEED=${1:-2600}
F=/tmp/neontide_lead_wants
LEAD=
# (the parent must be the script itself: "sh .../snap_smoke.sh TREE MSG", not a command line that merely names it)
if ps -o args= -p $PPID 2>/dev/null | awk '$1 ~ /(^|\/)(da)?sh$/ && $2 ~ /(snap_smoke|snap_resume_melee|snap_resume_smoke)\.sh$/ {f = 1} END {exit !f}'; then LEAD=1; fi
[ -n "$LEAD" ] && touch $F
while [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -lt "$NEED" ]; do [ -n "$LEAD" ] && touch $F; sleep 15; done
if [ -n "$LEAD" ]; then
  touch $F
  ( sleep 60; rm -f $F ) </dev/null >/dev/null 2>&1 &
fi
