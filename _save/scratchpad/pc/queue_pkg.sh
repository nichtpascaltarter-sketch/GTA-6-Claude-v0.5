#!/bin/sh
# Lead side of docs/PC_WORKER.md: add (or cancel) a package on origin/claude/pc-queue without touching the shared work
# tree (plumbing on a private index). usage: queue_pkg.sh add <id> <file.md> | cancel <id>
SP=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
cd /home/user/GTA-6-Claude-v0.5 || exit 2
git fetch -q origin claude/pc-queue || exit 2
P=$(git rev-parse origin/claude/pc-queue) || exit 2
export GIT_INDEX_FILE=$SP/pc/index; rm -f $GIT_INDEX_FILE; git read-tree $P || exit 2
case "$1" in
  add) [ -f "$3" ] || { echo "usage: queue_pkg.sh add <id> <file.md>" >&2; exit 2; }; B=$(git hash-object -w "$3") && printf '100644 %s\tqueue/%s.md\n' $B "$2" | git update-index --add --index-info; M="PC package $2 queued";;
  cancel) B=$(printf 'taken back by the lead %s\n' "$(date -u +%FT%TZ)" | git hash-object -w --stdin) && printf '100644 %s\tqueue/%s.cancelled\n' $B "$2" | git update-index --add --index-info; M="PC package $2 taken back";;
  *) echo "usage: $0 add <id> <file> | cancel <id>"; exit 2;;
esac
T=$(git write-tree); unset GIT_INDEX_FILE
C=$(printf '%s\n\nCo-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>\nClaude-Session: https://claude.ai/code/session_01Fsx2GTrhwusdowbWY9LHRP\n' "$M" | git commit-tree $T -p $P) || exit 2
git push -q origin $C:refs/heads/claude/pc-queue && echo "$M ($C)"
