#!/bin/sh
# wait_codex.sh PR SHA OUT: poll the PR's comments until the Codex summary says the code review of SHA completed (max ~30 min)
PR=$1; SHA=$2; OUT=$3; n=0
while [ $n -lt 60 ]; do
  gh api "repos/nichtpascaltarter-sketch/GTA-6-Claude-v0.5/issues/$PR/comments?per_page=50" > "$OUT" 2>/dev/null
  r=$(python3 -I -c '
import json,re,sys
try: d=json.load(open(sys.argv[1]))
except Exception: print("ERR"); sys.exit()
for c in d:
    m=re.search(r"codex-review-state:v1 (\{.*?\}) -->", c.get("body",""))
    if not m: continue
    s=json.loads(m.group(1))["code_review"]
    if s.get("commit_sha")==sys.argv[2] and s.get("status")=="completed":
        print("DONE findings=%s unresolved=%s" % (s.get("has_any_findings"), s.get("has_unresolved_findings"))); sys.exit()
    print("WAIT %s %s" % (s.get("commit_sha"), s.get("status"))); sys.exit()
print("WAIT none")' "$OUT" "$SHA")
  case "$r" in DONE*) echo "$r"; exit 0;; esac
  n=$((n+1)); sleep 30
done
echo "TIMEOUT last: $r"
