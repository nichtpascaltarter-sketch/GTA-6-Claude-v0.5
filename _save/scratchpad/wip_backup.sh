#!/bin/sh
# Back up the shared working tree (every agent's uncommitted work, unverified) to the branch claude/neon-tide-wip, so
# a container reset cannot lose it. Never merged anywhere: verified work reaches main only through snap_smoke.sh.
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5 || exit 2
IDX=$SP/wip_index
cp .git/index "$IDX"
GIT_INDEX_FILE="$IDX" git add -A || exit 1
T=$(GIT_INDEX_FILE="$IDX" git write-tree) || exit 1
if [ "$T" = "$(git rev-parse HEAD^{tree})" ]; then echo "nothing to back up"; exit 0; fi
C=$(git commit-tree $T -p HEAD -m "WIP backup of the shared working tree, $(date -u '+%Y-%m-%d %H:%M') UTC (unverified, not for merging)

Every agent's uncommitted work in progress on top of $(git rev-parse --short HEAD), kept so a container reset cannot lose it.
Verified snapshots reach main only through the lead's snapshot pipeline.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Fsx2GTrhwusdowbWY9LHRP") || exit 1
for d in 0 2 4 8 16; do
  sleep $d
  if git push -q --force origin $C:refs/heads/claude/neon-tide-wip 2>$SP/wip_push.log; then echo "backed up $C (tree $T)"; exit 0; fi
done
echo "WIP PUSH FAILED"; cat $SP/wip_push.log; exit 1
