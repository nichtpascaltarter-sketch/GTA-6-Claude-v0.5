#!/bin/sh
# Check-compile every shader entry point of a source tree (default: the repo) with the real d3dcompiler_47 under Wine
# (the renderer agent's shc.exe). usage: shc_tree.sh [treeDir]; exit 0 only if nothing failed.
T=${1:-/home/user/GTA-6-Claude-v0.5}
L=/tmp/shc_lead_list.txt
cd "$T" || exit 2
grep -rhoE 'load(CS|PS|VS|GS)\("[a-z_0-9]+\.hlsl", "[A-Za-z_0-9]+"' src/render src/ui src/game 2>/dev/null | sort -u | \
 sed -E 's/load(CS|PS|VS|GS)\("([^"]+)", "([^"]+)"/\2 \3 \1/' | awk '{t=tolower($3)"_5_0"; print $1, $2, t}' > $L
echo "matgen.hlsl csGenerate cs_5_0 GEN=0" >> $L
WIN=$(printf 'Z:%s/src/shaders' "$T" | tr '/' '\\')
WINEDEBUG=-all WINEPREFIX=/tmp/wine_lead WINEDLLOVERRIDES="d3dcompiler_47=n" nice -n 10 /usr/lib/wine/wine64 /tmp/fx/shc/shc.exe "$WIN" 'Z:\tmp\shc_lead_list.txt' 2>/dev/null \
 | grep -v "^WARN" | grep -v "warning X" | grep -v "^$" > /tmp/shc_lead_out.txt
grep -E "FAIL|MISSING" /tmp/shc_lead_out.txt | head -5
tail -1 /tmp/shc_lead_out.txt
tail -1 /tmp/shc_lead_out.txt | grep -q " 0 failed"
