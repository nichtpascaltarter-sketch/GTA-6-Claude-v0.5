#!/bin/bash
# Save every agent's work, byte for byte, to the branch claude/neon-tide-save, so the project can be resumed in a fresh
# container after a pause. The commit's root is the shared working tree (HEAD plus every agent's uncommitted work, not
# verified); _save/ holds the lead's scratchpad, the agents' private trees under /tmp, git bundles of the nested repos
# and the task list. Regenerable binaries (exes, objects, screenshots, audio renders, Wine prefixes) are listed with
# their size and sha256 in _save/MANIFEST_skipped.tsv instead of stored. The repository is public: conversation
# transcripts are not saved here, and files containing an email address or a token are left out (listed in the manifest).
# usage: save_all.sh [dry]
set -u
BASE=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173
SP=$BASE/scratchpad
REPO=/home/user/GTA-6-Claude-v0.5
ST=$BASE/save_stage
BR=claude/neon-tide-save
rm -rf $ST; mkdir -p $ST/bundles
cd $REPO || exit 2
SRC=($SP /tmp/city /tmp/faces /tmp/night /tmp/ai /tmp/fx /tmp/story2 /tmp/sp /tmp/int /tmp/world /tmp/ui /tmp/wl /tmp/d3d12bug /root/.claude/tasks)
EXIST=(); for d in "${SRC[@]}"; do [ -d "$d" ] && EXIST+=("$d"); done

# every file: size, octal mode, path (tab-separated); nested .git dirs are pruned (bundled below)
find "${EXIST[@]}" -name .git -prune -o -type f -printf '%s\t%m\t%p\n' 2>/dev/null > $ST/all.tsv
find "${EXIST[@]}" -name .git -prune -o -type l -printf '%p\t%l\n' 2>/dev/null > $ST/symlinks.tsv
awk -F'\t' -v keep=$ST/keep.tsv -v skip=$ST/skip.tsv -v noext=$ST/noext.tsv '
  { p = $3; n = p; sub(/.*\//, "", n); e = ""; if (n ~ /\./) { e = tolower(n); sub(/.*\./, "", e) } }
  e ~ /^(exe|o|obj|a|dll|lib|pdb|bmp|ppm|pgm|png|jpg|jpeg|wav|dds|raw|bin|zst|xz|gz|zip|bundle)$/ { print > skip; next }
  $1 > 52428800 { print > skip; next }            # (over 50 MB)
  e == "" { print > noext; next }                  # (checked for ELF below)
  { print > keep }' $ST/all.tsv
touch $ST/keep.tsv $ST/skip.tsv $ST/noext.tsv
while IFS=$'\t' read -r s m p; do
  if [ "$(head -c 4 "$p" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ]; then printf '%s\t%s\t%s\n' "$s" "$m" "$p" >> $ST/skip.tsv
  else printf '%s\t%s\t%s\n' "$s" "$m" "$p" >> $ST/keep.tsv; fi
done < $ST/noext.tsv
# privacy: nothing with an email address or a credential token goes to the public repository
cut -f3 $ST/keep.tsv | tr '\n' '\0' | xargs -0 grep -l -I -E '[A-Za-z0-9._%+-]+@(gmail|googlemail|outlook|hotmail|yahoo|icloud|proton)\.|gh[pousr]_[A-Za-z0-9]{20,}|github_pat_|sk-ant-|Bearer [A-Za-z0-9._-]{20,}|BEGIN [A-Z ]*PRIVATE KEY' 2>/dev/null > $ST/private.txt
if [ -s $ST/private.txt ]; then
  awk -F'\t' 'NR == FNR { bad[$0] = 1; next } bad[$3] { print > "'$ST/skip.tsv.priv'"; next } { print }' $ST/private.txt $ST/keep.tsv > $ST/keep2.tsv
  mv $ST/keep2.tsv $ST/keep.tsv; cat $ST/skip.tsv.priv >> $ST/skip.tsv 2>/dev/null
fi
echo "files: $(wc -l < $ST/all.tsv) total, keep $(wc -l < $ST/keep.tsv) ($(awk -F'\t' '{s+=$1} END {printf "%.0f MB", s/1048576}' $ST/keep.tsv)), skip $(wc -l < $ST/skip.tsv) ($(awk -F'\t' '{s+=$1} END {printf "%.0f MB", s/1048576}' $ST/skip.tsv)), private-content files left out: $(wc -l < $ST/private.txt)"
awk -F'\t' '$1 > 10485760 {printf "  big kept: %.0f MB %s\n", $1/1048576, $3}' $ST/keep.tsv | head -20
if [ "${1:-}" = dry ]; then sed 's/^/  private: /' $ST/private.txt | head -20; exit 0; fi

# nested git repos (the renderer's private trees) as bundles: their full history, byte for byte
find "${EXIST[@]}" -name .git -type d -prune 2>/dev/null | while read -r g; do
  d=${g%/.git}; n=$(echo "$d" | sed 's#^/##; s#/#_#g')
  if git -C "$d" bundle create "$ST/bundles/$n.bundle" --all >/dev/null 2>&1; then echo "bundle $d"; else echo "bundle FAILED $d (working files still saved)"; fi
done
# the manifest of what is not stored (size, sha256, path) and the symlinks
{ printf 'size\tsha256\tpath\n'; while IFS=$'\t' read -r s m p; do printf '%s\t%s\t%s\n' "$s" "$(sha256sum "$p" | cut -c1-64)" "$p"; done < $ST/skip.tsv; } > $ST/MANIFEST_skipped.tsv
cp $ST/symlinks.tsv $ST/SYMLINKS.tsv

# map a path on disk to its place in the saved tree
mapp() { case "$1" in "$SP"/*) echo "_save/scratchpad/${1#$SP/}";; /root/.claude/tasks/*) echo "_save/claude_tasks/${1#/root/.claude/tasks/}";; /tmp/*) echo "_save/tmp/${1#/tmp/}";; *) echo "_save/other${1}";; esac; }
: > $ST/index_info
cut -f3 $ST/keep.tsv > $ST/keep_paths
git hash-object -w --stdin-paths < $ST/keep_paths > $ST/keep_hashes || { echo "HASH FAILED"; exit 1; }
paste $ST/keep.tsv $ST/keep_hashes | while IFS=$'\t' read -r s m p h; do
  mode=100644; case "$m" in *[1357]?? | ?[1357]? | ??[1357]) mode=100755;; esac
  printf '%s %s\t%s\n' "$mode" "$h" "$(mapp "$p")" >> $ST/index_info
done
for f in $ST/bundles/*.bundle $ST/MANIFEST_skipped.tsv $ST/SYMLINKS.tsv $SP/RESTORE.md; do
  [ -f "$f" ] || continue
  case "$f" in *.bundle) dst="_save/bundles/$(basename $f)";; $SP/RESTORE.md) dst="_save/RESTORE.md";; *) dst="_save/$(basename $f)";; esac
  printf '100644 %s\t%s\n' "$(git hash-object -w "$f")" "$dst" >> $ST/index_info
done

# root = the shared working tree (as the WIP backup takes it), plus _save/
IDX=$ST/index
cp .git/index $IDX
GIT_INDEX_FILE=$IDX git add -A || exit 1
GIT_INDEX_FILE=$IDX git update-index --index-info < $ST/index_info || { echo "INDEX FAILED"; exit 1; }
T=$(GIT_INDEX_FILE=$IDX git write-tree) || exit 1
C=$(git commit-tree $T -p HEAD -m "Save point $(date -u '+%Y-%m-%d %H:%M') UTC: every agent's work, for resuming after a pause (not for merging)

Root: the shared working tree, HEAD $(git rev-parse --short HEAD) plus every agent's uncommitted work (unverified).
_save/: the lead's scratchpad (scripts, notes, resume/ notes per agent), the agents' private trees under /tmp,
git bundles of nested repos, the task list, and a manifest of the regenerable binaries left out.
See _save/RESTORE.md.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01Fsx2GTrhwusdowbWY9LHRP") || exit 1
echo "commit $C tree $T"
for d in 0 2 4 8 16; do
  sleep $d
  if git push --force origin $C:refs/heads/$BR > $ST/push.log 2>&1; then echo "SAVED $C to $BR"; exit 0; fi
  tail -3 $ST/push.log
done
echo "SAVE PUSH FAILED"; exit 1
