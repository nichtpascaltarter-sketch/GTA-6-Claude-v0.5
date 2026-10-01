# Faces agent: resume notes (2026-10-01, 21:20 UTC)

Work area: `/tmp/faces/`. Montages and other images: `scratchpad/faces/`. The shared tree is `/home/user/GTA-6-Claude-v0.5`; I never commit there.

## State

| What | State |
|---|---|
| Batch 1 (eyes, lids, lashes, strand brows, skin shading data, ears, beards, stubble, hairline) | Merged: snapshot 20, commit 2833ad0, main 46800db |
| Eye flip (`kShaderPupil = true`, plus anim_test shader-pupil checks) | Merged: snapshot 22, main 4b6357f, together with the renderer's character shading |
| Batch 2 (no more goggles round the eyes, brows, brow ridge, waterline, lips, temple hair) | Verified, **not sent and not in the shared tree** |
| Batch 3 (temple hollows with age, softer lower-lip border, skin undertones) | Dev only; anim_test passes; never built or run in game |

At wrap-up, main is 6780edd. My six files there are unchanged since 4b6357f, which hold batch 1 plus the flip. The working tree matches HEAD for all of them.

## Batch 2: ready to send

**Files** (6), in `/tmp/faces/batch2/`. The verified tree is `/tmp/faces/b2t5`, which is main 4b6357f plus these files.

| File | Goes to |
|---|---|
| face.cpp | src/anim/ |
| hair.cpp | src/anim/ |
| character.cpp | src/anim/ |
| bodymesh.cpp | src/anim/ |
| anim_test.cpp | tests/anim/ |
| preview.cpp | tests/anim/ |

**Message draft:** `/tmp/faces/msg/batch2.txt`. It starts with "faces snapshot-safe" and holds the file list, changelog, tests, montages, and an optional Dex stubble hook for `src/game/protagonists.h`.

**Changes**

*Eye region*
- Soft lid and orbit SDF blends: 3/4.5/5.5 mm became 8 mm.
- A fill at the medial canthus.
- Relaxed periorbital normals, and a lid crease carried by mesh normals (bodymesh.cpp, `buildBody`).
- Orbit paint only under the eye and at the inner corner.
- A darker lower waterline.
- A lighter lash line on men.
- Sclera x0.9 on very dark skin.

*Brows*
- `phBase` raised from 18.4 to 20 degrees.
- The tail is thinner: height 1.7 -> 1.4.
- The head is sparser.
- The skin tint is stronger in the brow body.
- Brow surface marches are about 2x faster (pruned primitive list `nearPrims`). Don't name it `near`: that is a Windows macro.

*Brow ridge*
- Moved forward 2-3.5 mm, x0.8 for women. The cornea now sits 6.4 mm behind the brow front on average.
- The renderer said this is enough. Keep the brow-ridge sky occlusion at 0.22, as the renderer asked.

*Hair (hair.cpp)*
- The soft front hairline is kept. At the temples and sides (`hairSideK`, `hairRamp`) the shell keeps a short thickness ramp and a hair-coloured edge.
- Full cards at the sides, and tips may fall only 1 mm past the hairline there.
- This fixes batch 1's dark and pale stripes beside the eyes (c17).
- Beards: rim 1.0 -> 0.6 mm, softer edge colour.

*Skin*
- Lip gloss 0.85 -> 0.65; the renderer agreed.
- Men's lips less red.
- Ear blush 0.55 -> 0.4.
- Older skin blotchier.
- Forehead lines clear of the frown lines.

**Verification done**

| Check | Result | Where |
|---|---|---|
| anim_test on b2t5 | ALL PASSED, including the new FaceShape test | `/tmp/faces/anim_test_b2c.log` |
| `tests/anim/carview.cpp` compile | compiles | |
| O2 build | exit 0 | `/tmp/faces/nt_b2c_o2.exe`, `build_b2c_o2.log` |
| Wine D3D12, 30 portraits on main's new shaders | no FATAL or vkd3d lines | `/tmp/faces/log_b2c.txt`; shots in `/tmp/faces/b2c/` |
| Crowd perf, tour stop 7, cpu ms peds | 4.22 (base 4.74, batch 1 4.47 / 4.62) | measured with `/tmp/faces/nt_b2_o2.exe` |

- The new FaceShape test fails on batch-1 code.
- The perf exe is from before the side-hair fix, which only changes hair colours and heights.
- Montages, main 4b6357f vs batch 2, both on new shaders: `scratchpad/faces/b2_c{0,1,27,7,17,38}.png`. The "before" shots are in `/tmp/faces/base5/`.

**To send (next session)**
1. Check that main still holds my files as at 4b6357f:
   `git diff --quiet 4b6357f HEAD -- src/anim/{face,hair,character,bodymesh}.cpp tests/anim/{anim_test,preview}.cpp`
   If anything changed, merge it first and re-run anim_test.
2. Copy the six files from `/tmp/faces/batch2/` into the working tree (write to a temp file, then `mv`).
3. Send `/tmp/faces/msg/batch2.txt` to "main".

## Batch 3 (dev)

The dev tree is `/tmp/faces/dev3`: main 4b6357f, plus batch 2, plus these changes. anim_test passes (`/tmp/faces/anim_test_dev3b.log`).

| Change | File | Notes |
|---|---|---|
| Temple hollows with age | face.cpp, `addHeadPrims` | OP_SUB ellipsoids behind the orbital rim; stronger on lean faces, weaker on heavy ones |
| Softer lower-lip border | face.cpp | lower-lip SDF blends 3 -> 4.8 / 4.2 mm; the tight blend left an AO groove that read as an outline |
| Skin undertone | character.cpp, `randomCharacter` | rosy vs olive/golden hue shift at constant luminance, from its own Rng stream so other draws keep their values; East Asian leans golden |

Still to do for batch 3:
- QUICK build and in-game shots.
- Look at faces under the new shading at night (4 m).

Older dev trees (`/tmp/faces/dev`, `dev5tree` and so on) carry other agents' old files. **Use dev3 only.**

## Commands

Every host compile waits for `sh tools/memfree.sh` to reach at least 2000. Run one game and one build of mine at a time.

- **anim_test:**
  `cd TREE && until [ "$(sh /home/user/GTA-6-Claude-v0.5/tools/memfree.sh)" -ge 2000 ]; do sleep 15; done; nice -n 5 g++ -O2 -std=c++17 -I src tests/anim/anim_test.cpp -o /tmp/faces/anim_test_X && (cd TREE && /tmp/faces/anim_test_X)`
- **preview** (`--protagonist 0|1`, `PREVIEW_SHADE=albedo|normal|diffuse` and `PREVIEW_WIRE=1` are in batch 2's preview.cpp):
  `g++ -O1 -std=c++17 -I src tests/anim/preview.cpp -o /tmp/faces/preview_X`
  then `/tmp/faces/preview_X out.ppm --seed S --role R --view face|face3|faceside --dist 0.42 --w 400 --h 400 --ss 2`
- **Game build:** `cd TREE && OUT=/tmp/faces/nt_X.exe sh build.sh`. Add `QUICK=1` for iteration builds.
- **Portrait shots** (30 shots: people 0, 1, 27, 7, 17, 38 at 0.5, 1 and 4 m by day; 1 and 4 m at night):
  `cd /tmp/faces && EXE=/tmp/faces/nt_X.exe TIMEOUT=3600 ./shoot2.sh NAME --viewer characters --protagonists --clip 0 --settle 14 --viewerlamp $(cat final_day.args) $(cat final_night.args)`
  Output: PNGs in `/tmp/faces/NAME/`, log in `/tmp/faces/log_NAME.txt`.
  For quick iteration use `iter2_day.args` and `iter2_night.args` (people 0, 27, 7, 17, 38, 10, 6). `fshots.py` makes new arg lists.
- **Perf:** `/tmp/faces/perf.sh /tmp/faces/nt_X.exe TAG`. This runs tour stop 7 and greps "cpu ms ... peds".
- **Montages:** `sh /tmp/faces/make_montages2.sh BEFORE_DIR AFTER_DIR TAG "label before" "label after"`. Output goes to `scratchpad/faces/TAG_c*.png`.

**Debug tools**, in `/tmp/faces/tools/`; build each from a tree with `-I src -I .`:

| Tool | What it shows |
|---|---|
| sdfview.cpp | face SDF in theta/phi map space, with the grid dots |
| sdfslice.cpp | eye sagittal slice |
| midslice.cpp | midline profile |
| eyedepth.cpp | cornea depth behind the brow front |
| browpos.cpp | brow, crease and margin heights |
| nrmjump.cpp | eye-region normal jumps |
| instrument.py | adds timing to a tree copy, for prof.cpp |

## Coordination

- **Renderer agent** (afef9ce4809b4c421; its work is on main):
  - Skin reads region, translucency, pores, oiliness, age and melanin bits. Lips need nonzero tissue fields; oiliness >= 1 on all skin.
  - Eyes are SM_EYE with feature shadows. Hair cards use DEPTH bits 20-22.
- **Lead:**
  - Report each batch with "faces snapshot-safe" plus the file list.
  - Never edit `src/game/*`.
  - `character.h` is getting additive fields from the animation agent. My batches don't touch it.
