#!/bin/sh
# Extract (file, entry, target) from loadCS/PS/VS/GS calls in src/render and check-compile them under wine.
cd /home/user/GTA-6-Claude-v0.5
grep -rhoE 'load(CS|PS|VS|GS)\("[a-z_0-9]+\.hlsl", "[A-Za-z_0-9]+"' src/render src/ui src/game 2>/dev/null | sort -u | \
 sed -E 's/load(CS|PS|VS|GS)\("([^"]+)", "([^"]+)"/\2 \3 \1/' | awk '{t=tolower($3)"_5_0"; print $1, $2, t}' > /tmp/fx/shc/list.txt
echo "matgen.hlsl csGenerate cs_5_0 GEN=0" >> /tmp/fx/shc/list.txt
# every material generator variant used by the material library (materials.cpp: {MAT_X, gen, ...})
for g in $(grep -oE '\{MAT_[A-Z_]+, [0-9]+,' src/render/materials.cpp | awk -F', ' '{print $2}' | tr -d ',' | sort -un); do
  [ "$g" = "0" ] || echo "matgen.hlsl csGenerate cs_5_0 GEN=$g" >> /tmp/fx/shc/list.txt
done
WINEDEBUG=-all WINEPREFIX=/tmp/wine_fx WINEDLLOVERRIDES="d3dcompiler_47=n" nice -n 10 /usr/lib/wine/wine64 /tmp/fx/shc/shc.exe 'Z:\home\user\GTA-6-Claude-v0.5\src\shaders' 'Z:\tmp\fx\shc\list.txt' 2>/dev/null | grep -v "^WARN" | grep -v "warning X" | grep -v "^$"
