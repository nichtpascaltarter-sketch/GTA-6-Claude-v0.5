#!/bin/sh
# The sign-off table: every file that differs from HEAD bcb8a71, with its base blob and new blob (WRITE=1 writes the
# new blobs into the shared repository's object store with git hash-object -w; nothing else is touched there)
R=/home/user/GTA-6-Claude-v0.5; T=/tmp/upd2/tree; B=/tmp/upd2/base; HEAD=bcb8a71
cd $T
diff -rq $B $T 2>/dev/null | grep -v -e "^Only in $T: bin$" -e "^Only in $T: build$" -e "^Only in $T/bin" -e "^Only in $T/build[/:]" | while read -r line; do
  case "$line" in
    Files*) f=$(echo "$line" | sed "s|^Files $B/||; s| and .*||");;
    "Only in $T"*) d=$(echo "$line" | sed "s|^Only in $T/\{0,1\}||; s|: |/|"); f=$d;;
    "Only in $B"*) d=$(echo "$line" | sed "s|^Only in $B/\{0,1\}||; s|: |/|"); f=$d;;
  esac
  f=${f#/}
  base=$(git -C $R rev-parse -q --verify "$HEAD:$f" 2>/dev/null || echo "-")
  if [ -f "$T/$f" ]; then
    if [ "${WRITE:-0}" = 1 ]; then new=$(git -C $R hash-object -w "$T/$f"); else new=$(git -C $R hash-object "$T/$f"); fi
  else
    new="deleted"
  fi
  printf '%s %s %s\n' "$f" "$base" "$new"
done | sort
