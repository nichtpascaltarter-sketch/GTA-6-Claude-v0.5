#!/bin/sh
# usage: timings.sh log  -> last "Pass timings" block with percentages
f=$1
n=$(grep -n "Pass timings" $f | tail -1 | cut -d: -f1)
[ -z "$n" ] && { echo "no pass timings in $f"; exit 1; }
sed -n "${n}p" $f
sed -n "$((n+1)),$((n+30))p" $f | tr -d "\r" | awk '/ ms$/ { name=$0; sub(/ +[0-9.]+ ms$/, "", name); v=$(NF-1); if (name=="total") tot=v; else { names[++k]=name; vals[k]=v } } /^$/ {exit} END { for (i=1;i<=k;i++) printf "%-18s %9.2f ms %5.1f%%\n", names[i], vals[i], 100*vals[i]/tot; printf "%-18s %9.2f ms\n", "total", tot }'
