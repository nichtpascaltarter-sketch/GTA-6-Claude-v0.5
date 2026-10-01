#!/bin/sh
# Commit the last build-verified snapshot tree on top of HEAD (message from $1), then re-sync the index to HEAD
# (working tree untouched: later edits simply stay modified). If origin/main has commits this branch lacks (a PR was
# merged since the branch was last brought up to main), hold instead of stacking on the merged history: the lead
# fast-forwards the branch to main first, then commits the held tree (listed in snap_hold.txt).
set -e
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5
TREE=$(cat $SP/snap_tree_ok)
git fetch -q origin main || true
if ! git merge-base --is-ancestor origin/main HEAD; then
  echo "$(date -u +%H:%M) held tree $TREE (origin/main moved): $(printf '%s' "$1" | head -1)" >> $SP/snap_hold.txt
  echo "HOLD: origin/main has moved past this branch; fast-forward it first"
  exit 1
fi
MSG="$1

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Fsx2GTrhwusdowbWY9LHRP"
C=$(git commit-tree $TREE -p HEAD -m "$MSG")
git update-ref refs/heads/claude/neon-tide $C
git reset -q
git log --oneline -1
