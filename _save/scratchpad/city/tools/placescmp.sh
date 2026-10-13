#!/bin/sh
# compare story place resolution and building counts between two game logs
for f in "$1" "$2"; do
  echo "== $f"
  grep -E "Story places resolved|Story place near|Missions registered|Places: layout|interiors? planned|Buildings: [0-9]+ \(|block infill|restyled" "$f" | sed 's/^\[ *[0-9.]*\] //' | cut -c1-260
done
