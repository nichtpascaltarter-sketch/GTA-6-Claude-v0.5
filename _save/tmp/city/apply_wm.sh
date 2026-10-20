#!/bin/bash
# Applies the world model v0 (city agent) to a tree: the shared tree once the road fix (roads.cpp c5bc0aab) and step 4.1
# have landed. Checks every base blob and that no new file exists yet, then writes them all; nothing is written on a mismatch.
T=${1:?usage: apply_wm.sh <tree> [repo holding the blobs]}
R=${2:-/home/user/GTA-6-Claude-v0.5}
cd "$T" || exit 1
fail=0
base() { local h=$(git hash-object "$1" 2>/dev/null); [ "${h:0:8}" = "$2" ] || { echo "base mismatch: $1 is ${h:0:8}, expected $2"; fail=1; }; }
absent() { [ ! -e "$1" ] || { echo "already exists: $1"; fail=1; }; }
base src/world/roads.cpp c5bc0aab
base src/world/roads.h eee40e8e
base src/world/ntwschema.h 862eac69
base src/world/ntwschema.cpp 906d1d0f
base docs/NTW_FORMAT.md d318643d
base tests/world/world_format_test.cpp 90f983c5
base .github/workflows/windows-build.yml afe28696
absent src/world/roadnet.cpp
absent src/world/worldmodel.h
absent src/world/worldmodel.cpp
absent src/world/worldmodel_roads.cpp
absent tests/world/worldmodel_test.cpp
absent tests/world/fixtures/model/districts/cl/blocks/b001/block.ntw
absent tests/world/fixtures/model/districts/cl/district.ntw
absent tests/world/fixtures/model/districts/cl/roads.ntw
absent tests/world/fixtures/model/districts/sb/district.ntw
absent tests/world/fixtures/model/districts/sb/roads.ntw
absent tests/world/fixtures/model/lib/catalog.ntw
absent tests/world/fixtures/model/lib/signals/fourway.ntw
absent tests/world/fixtures/model/places/places.ntw
absent tests/world/fixtures/model/transit/bus.1.ntw
absent tests/world/fixtures/model/trunk/roads.ntw
absent tests/world/fixtures/model/world.ntw
[ $fail = 0 ] || { echo "nothing written"; exit 1; }
mkdir -p "$(dirname src/world/roads.cpp)" && git -C "$R" cat-file blob 120a1113de78dd3f496e0ea4bad08548df66ccb6 > src/world/roads.cpp
mkdir -p "$(dirname src/world/roads.h)" && git -C "$R" cat-file blob 8f46021fd8ca52ed4de27d2486c02b0454188e90 > src/world/roads.h
mkdir -p "$(dirname src/world/ntwschema.h)" && git -C "$R" cat-file blob 29d50e6add068af2830f774f05db1442a6397d85 > src/world/ntwschema.h
mkdir -p "$(dirname src/world/ntwschema.cpp)" && git -C "$R" cat-file blob 667e43c32b05c34614481abbd692acba53662fcb > src/world/ntwschema.cpp
mkdir -p "$(dirname docs/NTW_FORMAT.md)" && git -C "$R" cat-file blob 1763c8dc580b6ee478c384c71297174790503e15 > docs/NTW_FORMAT.md
mkdir -p "$(dirname tests/world/world_format_test.cpp)" && git -C "$R" cat-file blob 24ff960c56f4376c7960134be6e5f186b66e3f03 > tests/world/world_format_test.cpp
mkdir -p "$(dirname .github/workflows/windows-build.yml)" && git -C "$R" cat-file blob 5d63b13a58354c8e87d1d98a063bc77a6725eff0 > .github/workflows/windows-build.yml
mkdir -p "$(dirname src/world/roadnet.cpp)" && git -C "$R" cat-file blob 610c4236b9b6d33cdd67b0951d92564e2f9245d6 > src/world/roadnet.cpp
mkdir -p "$(dirname src/world/worldmodel.h)" && git -C "$R" cat-file blob bbaa9486ee054c33024792cc610207e5f1cc18ab > src/world/worldmodel.h
mkdir -p "$(dirname src/world/worldmodel.cpp)" && git -C "$R" cat-file blob 584aa2487f91a7418cf0fbb4f5c2d99b632b3f84 > src/world/worldmodel.cpp
mkdir -p "$(dirname src/world/worldmodel_roads.cpp)" && git -C "$R" cat-file blob f2230630e1dddc2a129bf9396fe2270ae5df4659 > src/world/worldmodel_roads.cpp
mkdir -p "$(dirname tests/world/worldmodel_test.cpp)" && git -C "$R" cat-file blob 81f620d74b77e9bbe556324a36417a4a0da0f6b3 > tests/world/worldmodel_test.cpp
mkdir -p "$(dirname tests/world/fixtures/model/districts/cl/blocks/b001/block.ntw)" && git -C "$R" cat-file blob eabd996ab8615393c3879f7985fceb1598d0f317 > tests/world/fixtures/model/districts/cl/blocks/b001/block.ntw
mkdir -p "$(dirname tests/world/fixtures/model/districts/cl/district.ntw)" && git -C "$R" cat-file blob 5cf39548d4c4a6a5261f6090a6992b4851dc170f > tests/world/fixtures/model/districts/cl/district.ntw
mkdir -p "$(dirname tests/world/fixtures/model/districts/cl/roads.ntw)" && git -C "$R" cat-file blob e49cf828b2d6031c260395ef124186ba1539f1ff > tests/world/fixtures/model/districts/cl/roads.ntw
mkdir -p "$(dirname tests/world/fixtures/model/districts/sb/district.ntw)" && git -C "$R" cat-file blob 2ecc811423aeb6c2a674fb29786e20fce270bf18 > tests/world/fixtures/model/districts/sb/district.ntw
mkdir -p "$(dirname tests/world/fixtures/model/districts/sb/roads.ntw)" && git -C "$R" cat-file blob 708ddccb20c9ce17d80d884bbf64ad91872a4b08 > tests/world/fixtures/model/districts/sb/roads.ntw
mkdir -p "$(dirname tests/world/fixtures/model/lib/catalog.ntw)" && git -C "$R" cat-file blob cb8d504f6f062583ed305a1aed6c6478b4af9a8d > tests/world/fixtures/model/lib/catalog.ntw
mkdir -p "$(dirname tests/world/fixtures/model/lib/signals/fourway.ntw)" && git -C "$R" cat-file blob e55c41708077b95a5f03ac2343d135bedf9a8d0c > tests/world/fixtures/model/lib/signals/fourway.ntw
mkdir -p "$(dirname tests/world/fixtures/model/places/places.ntw)" && git -C "$R" cat-file blob 54c55b63bd82fd3adb7a0abea1f6cd2590d00976 > tests/world/fixtures/model/places/places.ntw
mkdir -p "$(dirname tests/world/fixtures/model/transit/bus.1.ntw)" && git -C "$R" cat-file blob 78a32b778af36519606dbfbfca60e1f5927a7010 > tests/world/fixtures/model/transit/bus.1.ntw
mkdir -p "$(dirname tests/world/fixtures/model/trunk/roads.ntw)" && git -C "$R" cat-file blob ad49d21380a09c890fc19f5c308c1ccbbb6857ae > tests/world/fixtures/model/trunk/roads.ntw
mkdir -p "$(dirname tests/world/fixtures/model/world.ntw)" && git -C "$R" cat-file blob f21ea7f56f0695f70cc700468f30dab3de5ddc70 > tests/world/fixtures/model/world.ntw
echo "world model v0 applied: $(grep -c "^mkdir -p" "$0") files"
