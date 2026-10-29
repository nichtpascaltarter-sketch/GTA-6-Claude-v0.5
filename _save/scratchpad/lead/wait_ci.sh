#!/bin/sh
# wait_ci.sh SHA OUT: poll the commit's check runs until all are completed (max ~25 min), then print each one's conclusion
SHA=$1; OUT=$2; n=0
while [ $n -lt 50 ]; do
  gh api "repos/nichtpascaltarter-sketch/GTA-6-Claude-v0.5/commits/$SHA/check-runs?per_page=50" > "$OUT" 2>/dev/null
  r=$(python3 -I -c '
import json,sys
try: d=json.load(open(sys.argv[1]))
except Exception: print("ERR"); sys.exit()
runs=d.get("check_runs",[])
if not runs: print("NONE"); sys.exit()
if all(r["status"]=="completed" for r in runs):
  print("DONE " + "; ".join("%s=%s" % (r["name"], r["conclusion"]) for r in runs))
else: print("WAIT")' "$OUT")
  case "$r" in DONE*) echo "$r"; exit 0;; esac
  n=$((n+1)); sleep 30
done
echo "TIMEOUT last: $r"
