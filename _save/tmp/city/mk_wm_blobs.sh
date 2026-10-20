#!/bin/bash
# Writes the world model v0 blobs into the shared repo's object store (git hash-object -w) and the list + apply script.
S=/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
R=/home/user/GTA-6-Claude-v0.5
cd $S/wm || exit 1
CHANGED="src/world/roads.cpp:c5bc0aab src/world/roads.h:eee40e8e src/world/ntwschema.h:862eac69 src/world/ntwschema.cpp:906d1d0f docs/NTW_FORMAT.md:d318643d tests/world/world_format_test.cpp:90f983c5 .github/workflows/windows-build.yml:afe28696"
NEW="src/world/roadnet.cpp src/world/worldmodel.h src/world/worldmodel.cpp src/world/worldmodel_roads.cpp tests/world/worldmodel_test.cpp $(cd $S/wm && find tests/world/fixtures/model -type f | sort | tr '\n' ' ')"
L=/tmp/city/wm_blobs.txt
A=/tmp/city/apply_wm.sh
echo "# world model v0 blobs (city agent): path blob [base]; on bcb8a71 + road fix c5bc0aab + step 4.1" > $L
{
  echo '#!/bin/bash'
  echo '# Applies the world model v0 (city agent) to a tree: the shared tree once the road fix (roads.cpp c5bc0aab) and step 4.1'
  echo '# have landed. Checks every base blob and that no new file exists yet, then writes them all; nothing is written on a mismatch.'
  echo 'T=${1:?usage: apply_wm.sh <tree> [repo holding the blobs]}'
  echo 'R=${2:-/home/user/GTA-6-Claude-v0.5}'
  echo 'cd "$T" || exit 1'
  echo 'fail=0'
  echo 'base() { local h=$(git hash-object "$1" 2>/dev/null); [ "${h:0:8}" = "$2" ] || { echo "base mismatch: $1 is ${h:0:8}, expected $2"; fail=1; }; }'
  echo 'absent() { [ ! -e "$1" ] || { echo "already exists: $1"; fail=1; }; }'
} > $A
for cb in $CHANGED; do
  f=${cb%%:*}; b=${cb##*:}
  h=$(git -C $R hash-object -w $S/wm/$f)
  echo "$f $h on $b" >> $L
  echo "base $f $b" >> $A
done
for f in $NEW; do
  h=$(git -C $R hash-object -w $S/wm/$f)
  echo "$f $h new" >> $L
  echo "absent $f" >> $A
done
echo '[ $fail = 0 ] || { echo "nothing written"; exit 1; }' >> $A
grep -v '^#' $L | while read f h rest; do
  echo "mkdir -p \"\$(dirname $f)\" && git -C \"\$R\" cat-file blob $h > $f" >> $A
done
echo 'echo "world model v0 applied: $(grep -c "^mkdir -p" "$0") files"' >> $A
chmod +x $A
cat $L
