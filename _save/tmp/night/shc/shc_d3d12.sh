#!/bin/sh
# Compile-check every shader entry point of a Direct3D 12 tree with d3dcompiler_47 under Wine, as the game does:
# vs/ps/cs_5_1, O3 + strictness + unbounded descriptor tables. Covers the loaders in src/render, src/ui, src/game and
# src/gfx (mip generator, self-test) and every matgen GEN variant.
# usage: shc_d3d12.sh [tree dir] [wine prefix]; exit 0 only if nothing failed.
T=${1:-/home/user/GTA-6-Claude-v0.5}
PFX=${2:-/tmp/wine_lead}
D=$(cd "$(dirname "$0")" && pwd)
L=$D/list_$$.txt
cd "$T" || exit 2
grep -rhoE 'load(CS|PS|VS)\("[a-z_0-9]+\.hlsl", "[A-Za-z_0-9]+"' src/render src/ui src/game src/gfx 2>/dev/null | sort -u | \
  sed -E 's/load(CS|PS|VS)\("([^"]+)", "([^"]+)"/\2 \3 \1/' | awk '{print $1, $2, tolower($3) "_5_1"}' > "$L"
n=$(grep -c '^#elif GEN ==' src/shaders/matgen.hlsl)
i=0
while [ $i -le "$n" ]; do echo "matgen.hlsl csGenerate cs_5_1 GEN=$i" >> "$L"; i=$((i+1)); done
[ -x "$D/shc51.exe" ] || x86_64-w64-mingw32-g++ -O1 -std=c++17 -static "$D/shc51.cpp" -o "$D/shc51.exe" || exit 2
WIN=$(printf 'Z:%s/src/shaders' "$T" | tr '/' '\\')
WLIST=$(printf 'Z:%s' "$L" | tr '/' '\\')
WINEDEBUG=-all WINEPREFIX=$PFX WINEDLLOVERRIDES="d3dcompiler_47=n" nice -n 10 /usr/lib/wine/wine64 "$D/shc51.exe" "$WIN" "$WLIST" 2>/dev/null > "$D/out_$$.txt"
rc=$?
grep -E 'FAIL|MISSING' -A6 "$D/out_$$.txt" | head -40
tail -1 "$D/out_$$.txt"
rm -f "$L" "$D/out_$$.txt"
exit $rc
