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


## Update 2026-10-05 (after the reboot)

| What | State |
|---|---|
| Batch 2 | Merged: snapshot 25, PR #25, main 3319337 (with the Dex stubble line in protagonists.h) |
| Batch 3 (beards wrap the jaw, smooth side hairline, women's short sideburns, temple hollows, undertones, softer lower lip, PREVIEW_LIGHT) | Sent; snapshot 28 (tree dd65296) verifying. Files in `/tmp/faces/batch3/`, message `/tmp/faces/msg/batch3.txt` |
| Batch 4 | In progress |

Sign-offs: hash with `git hash-object -w` (the lead asked: it writes the blob into the object store).

Batch 4 work:
- `/tmp/faces/dev6`: main 8ee5699 + batch 3 + an A/B switch in clothing.cpp `emitGarment` (env `FACES_AB`: 1 = no rolled-hem lip on hair shells, 2 = also the skin's normals where the shell lies close). Test build `/tmp/faces/nt_ab.exe` (QUICK); runs `ab0/ab1/ab2` via `/tmp/faces/chain18.sh` (shots: c27/c38 at 0.5 and 1 m, day and night; plus 0.3 m brow close-ups of c0, c17, c1, c7 by day). Never ship the getenv.
- `/tmp/faces/dev7`: main 8ee5699 + batch 3 + smoother curly coils (hair.cpp `buildCoils`: 6 stations a turn instead of 3, 5 sides instead of 4). anim_test passes; build time unchanged.
- Open questions: the night cheek line on bearded men (the A/B decides between the hem lip and the shell normals); brows read faint and "outlined" in game at 0.5 m since batch 2 (raised, thinner tail, sparser head) under the new card shading: check the 0.3 m shots, then restore some density / strengthen the brow skin tint.

## Update 2026-10-05 18:40 (batch 4 in progress)

- Batch 3 merged: PR #28, main **91a6aba**. Base batch 4 on it (lead's instruction). `/tmp/faces/b4ref` = git archive 91a6aba.
- A/B result (ab0/ab1/ab2 in /tmp/faces): the rolled hem on hair shells makes the cheek line crisper, but the line stays without it.
  Root causes found: (1) the hem lip / depth step at the shell edge; (2) the renderer darkens the LOD0 hair shell
  (dynamic.hlsl psDynamic M_HAIR with params2.y: albedo x0.8 x strand 0.72..1.15, ao x0.6 x clump) so the beard shell's
  skin-coloured edge renders ~25-40% darker than the skin = the line.
- `/tmp/faces/dev8` = 91a6aba + coils + batch-4 changes:
  - clothing.cpp emitGarment: MAT_HAIR shells: no rolled hem; boundary vertices placed 0.05 mm under the skin (after normals).
  - hair.cpp: kBeardEdgeLift 1.33 (beard shell edge colour lifted; inverse stored in v.alpha); edge mix 0.2 -> 0.08 hair.
  - character.cpp stripForLod: LOD>=1 undoes the lift (col *= alpha) for F_BEARD MAT_HAIR shell vertices.
  - face.cpp addBrow: brow skin tint stronger (0.36/0.18/0.14 -> 0.44/0.2/0.2).
  - anim_test ALL PASSED (/tmp/faces/anim_test_d8.log). Preview bins: preview_d8b. QUICK build -> /tmp/faces/nt_d8.exe.
- Finding for the lead (renderer-owned): hair cards share one dither threshold per pixel (ignTemporal), so overlapping
  cards cover only max(cov) (~0.4-0.5): brows/beards/lashes can't get denser by adding cards.
- Debug-view runs queued: /tmp/faces/chain19.sh (dbg11 AO, dbg4 shadow, dbg1 albedo; nt_ab.exe FACES_AB=1).

## Update 20:35

- In-game d8a (dev8t, lift 1.33) and d8b (lift 1.6): the cheek line remains as a 1-2 px dark TROUGH on the skin just
  outside the beard shell edge (profiles: py/seg.py on c38_0p5m_night 422,304->438,288). The lift only brightens the
  shell side. Albedo debug (d8dbg1) shows NO line; SSAO (dbg11) no line; so it is lighting (lamp feature shadow or BRDF).
- Diagnosis runs queued (chain22.sh, nt_d8t2.exe from dev8t): FACES_DBG=1 shell shaded as skin, 2 no beard cards, 3 both.
- Brows: found the real cause of faint brows: the brow tint sat 0.35h above the cards and between head-grid rows 2.4 deg
  apart (w <= 0.14 in the body). dev8 now: +2 head rows (19.6, 22.15 deg), tint aligned with the cards' band and
  box-filtered over each vertex's row span (w up to ~0.84). bodymesh.cpp relax j1 = rowBrow + 2. anim_test gains a
  brow-tint check (fails on main: 0.96/0.88; dev8 <= 0.54). 4 rows cost ~9% build CPU, 2 rows ~1%.
- Renderer findings sent to the lead (night goggles, card dither, cards not feature-shadowed, shell card shade).

## Update 22:30 (batch 4 final candidate = /tmp/faces/dev8)

- Diagnosis done: the cheek line is the hair BRDF's wrap on the beard shell vs the skin terminator (renderer); sent to
  the lead (with q0 / dbgv1 / dbgv2 evidence). Lift reverted (made a seam by day).
- dev8 final: face.cpp (2 brow rows 19.6/22.15, aligned box-filtered brow tint), bodymesh.cpp (relax j1 = rowBrow+2),
  clothing.cpp (hair shells: edge flush with the skin, no rolled hem; thin edge shades with the skin's normal),
  hair.cpp (coils 6 stations / 5 sides; comment), anim_test (brow tint check), preview (PREVIEW_FEATSHADOW,
  PREVIEW_SHADE=featshadow). character.cpp = main.
- anim_test ALL PASSED (/tmp/faces/anim_test_b4.log). O2 build -> /tmp/faces/nt_b4_o2.exe (chain21: 30 portraits -> b4,
  perf -> perf_b4). chain26: main 91a6aba O2 build (b4ref -> nt_m91_o2.exe) + same portraits -> m91 (the "before").

## Update 00:55 (2026-10-06)

- Batch 4 SIGNED OFF (message /tmp/faces/msg/batch4.txt; files /tmp/faces/batch4/; blobs written with -w). Lead: queued
  for snapshot 33 (with AI sets 3+4), after snapshot 32. Main now 6afc8b2 (no changes to my files since 91a6aba).
- Batch 5 (pale brows by day): /tmp/faces/dev9 = dev8 + brow cards depth 3..5 (hash per card) + lash cards depth 3/4
  (lash edit may have missed the QUICK build nt_d9.exe at 00:50 - verify in the final build). anim_test passes.
  Test shots: chain28 -> /tmp/faces/d9 (brow.args: 0.5/1 m day c0 c1 c7 c17 c38 + 1 m night c0 c17 c38); compare with b4.
- Finding: at 1 m day the batch-4 tint makes c17's brow ~20% darker than the forehead (main: invisible).
- Next idea if depth is not enough: steeper herringbone (bodyUp 0.55/-0.3 -> ~0.7/-0.45 with a sharper switch).

## Update 01:35 (after the 00:57 container restart)

- Main = 8fb1e66 (city only since 6afc8b2). Batch 4 relaunched as snapshot 33 by the lead.
- Batch 5 test (d9: brow depth 3..5 [+ lash depth 3/4, maybe not in that QUICK exe]) vs b4: small extra darkening;
  the big step at 0.5 m day came from batch 4's tint (c17: grey hatched band -> solid dark brow).
- Batch 5 candidate = /tmp/faces/b5tree: main 8fb1e66 + batch 4 files + face.cpp from /tmp/faces/dev10
  (brow depth 3..5, lash depth 3/4 (lower 3), steeper herringbone bodyUp 0.7/-0.45 with sstep(0.38,0.62,v)).
  anim_test (dev10) ALL PASSED. chain30.sh: O2 build -> nt_b5_o2.exe, 30 portraits -> b5, perf -> perf_b5.
  Background: anim_test + carview on b5tree (anim_test_b5.log).
- Possible next: c17's forehead lines + frown lines form a "boxed" pattern in preview close-ups (wrinkle channel).

## Update 03:05 (2026-10-06)

- Batch 4 is on main d3c2c3f (PR #33). Batch 5 SIGNED OFF (face.cpp only, blob 6c37695b..., written with -w; in the
  shared tree; message /tmp/faces/msg/batch5.txt). anim_test on d3c2c3f + batch 5: ALL PASSED (anim_test_b5t3.log).
- Next (batch 6): the 1 m day read. c17 profile (lit vs albedo, b5 vs b5alb at x 462-468): bright 1-px line above the
  brow (ratio 1.12 vs forehead 0.89), the upper lid ~1.2 -> brow outlined in pale above a pale lid. Find the source
  (normals / skin gloss / cards) in the preview first. Also: c38's brow "visor" ledge (a horizontal line temple to
  temple at 0.5-1 m by day).

## Update 04:40 (batch 6 in progress)

- Lead: batch 5 queued for snapshot 34 (with the animation agent's pass-by fix); my next step (the 1 m pale read) OK.
- Diagnosis (private shader override /tmp/faces/shd_dbg/lighting.hlsl via --shaderdir, debug views 30/31/32; never
  shipped): by day the lids' brightness is direct sun on up-facing lid surfaces (NoL), no feature shadow, ambient
  uniform. The 1-px line above the brow is NOT in the lighting terms: post sharpening (post.cpp p3.z 0.35) halo.
- Found: a 6-10 mm deep TRENCH beside each eye at eye level (socket carve with no lateral orbital rim; frontprof
  tool) = the band across the face (night goggles at 4 m, visor by day). And the lower lid margin vertices took the
  margin strip's normal (+68 deg up): the pale crescent under each eye.
- /tmp/faces/dev11 = d3c2c3f + batch 5 + face.cpp: lateral orbital rim cones (after the lids in addHeadPrims) +
  lower margin normal blend 0.6 (addLidDetails) + anim_test trench check (fails on base 6-10 mm, dev11 <= 2.5 mm).
  anim_test ALL PASSED. chain33.sh: QUICK build nt_d11.exe + 30 portraits -> /tmp/faces/d11 (compare b5).
- Tools: /tmp/faces/tools/{pupilprof,topslice,frontprof}.cpp; py helpers chans.py, vprof3.py.
- 05:10: batch 5 merged (PR #34): main = cda29a1 (= d3c2c3f + batch 5 face.cpp + src/game/peds.cpp). Base batch 6 on
  cda29a1 (my files there equal dev11's base). Final verification tree: git archive cda29a1 + batch-6 files.

## Update 06:45 (batch 6 signed off)

- Batch 6 SIGNED OFF (message /tmp/faces/msg/batch6.txt; files /tmp/faces/batch6/; blobs face.cpp a8610b8c...,
  anim_test.cpp a60c092c..., written with -w, copies in the shared tree). Main is 9e7ace0 (city props only since cda29a1).
  Verified on b6tree (cda29a1 + 2 files): anim_test (new trench check), carview, O2 nt_b6_o2.exe, 30 portraits -> b6
  (0 FATAL), perf peds 3.21 (noise), montages b6_c*.png, b6_lowerlids_0p5m.png, b6_preview_sockets.png.
- Running: chain36 (night diag views 33/34/lit at 1920x1080 -> ndiag_v33/v34/v0), then chain37 (ground-bounce fill
  prototype /tmp/faces/shd_fill/lighting.hlsl: nfill at 1920, b6fill = 12 night portraits at 960). Then send the
  renderer proposal (exact hunk) to the lead.
- Dropped: brow ridge two-segment arch (dev12: bulged the ridge forward at the joint), brow tint extension (not
  anatomically motivated).
- 07:00 lead: batch 6 accepted (snapshot 37 with city 6+8+9). Night sockets: DON'T build my own bounce: snapshot 36 =
  renderer's character-shading backlog (lighting.hlsl ad2ceab2: kLampBounce 0.2 for person pixels, kFeatureBounce 0.1
  feature-shadow floor, shell wrap 0.15, face-hair feature shadows; dynamic.hlsl ea97d07d: per-card dither offset =
  cards accumulate). Test against it; if sockets still too dark at 1/4 m propose kLampBounce/tweak with before/after;
  re-check brow darkness (cards accumulate now: brows may read too dark). Sharpen halo passed to the night-lighting
  renderer agent. Index fixed by the lead.
- Killed my chain36/37 (old-shader night diag, own bounce prototype). chain38: 30 portraits with /tmp/faces/shd_s36
  (copies of the two renderer files) via --shaderdir on nt_b6_o2.exe -> /tmp/faces/b6s36.
- Measures: py/brows.py (fixed spots at 1 m: eyes y268, brows 255-264, forehead 242-250, eye cols 466/494),
  py/sockets.py. b6 (old shaders): day brows 0.7-2.6 stops below the forehead (c17 0.7 = pale); night sockets 3.9-4.9.
- 07:30: s36 shaders tested (b6s36, 0 FATAL): night sockets 1 m 2.8-4.0 stops (was 3.9-4.9), 4 m eye band 1.7-2.0
  (was 2.0-3.0) -> keep kLampBounce 0.2 (sent to lead). Brows at 0.5 m now crisp black lines (cards accumulate: ~6
  layers ~95%), lashes heavier. Batch 7 = brow card density ~x0.55, brow tint ~x0.5, lashes ~x0.7 (+ preview.cpp
  card dither = per-card offset, coverage port of the current shader).
- /tmp/faces/dev13 = 9e7ace0 + batch 6 + s36 shaders (in src/shaders) + dev knob FACES_BROWK=dens,tint,lash (getenv:
  NEVER SHIP). chain39: QUICK build nt_d13.exe, shots d13k1 (0.55,0.55,0.7) and d13k3 (0.6,0.4,0.7) with brow2.args
  (0.5m day, 1m day, 1m night x 6 people). Compare with b6s36 (= K0 under the same shaders).
- 08:20 lead: kLampBounce stays 0.2; snapshot 36 merges as is; batch 7 = delta on a8610b8c (main after snapshot 37).
- anim_test FaceHairCoverage (dev13/b7tree): renderer compositing model; brow body coverage at batch 6 values 0.98-0.99
  (solid), lash line 0.54-0.64. k4 (dens 0.38, tint 0.5, lash 0.75) in game: 0.5 m natural but 1 m faint (c17 0.6
  stops below forehead again). Round 2 chain41: k6 (0.55,0.85,0.75: model brow 0.85-0.91) and k7 (0.45,1.0,0.75:
  0.78-0.85) -> d13k6, d13k7. b7tree prepared with placeholders BROWDENS, BROWTINT0/1, LASH0, LASH1M/F, LASHLO.
- 08:50: batch 6 merged (PR #37): main = b907098 (face a8610b8c, anim_test a60c092c, s36 shaders). Base batch 7 on b907098 (recreate b7tree from it + my 3 files).
- 09:40: picked k7: brow cards x0.45 (dens Lerp(0.315,0.45)), tint unchanged, lashes x0.75 (0.71 / Lerp(0.24,0.41) /
  0.24). In game (s36 shaders): 1 m day brows 0.7-2.4 stops below forehead (= old-shader levels), 0.5 m strand texture
  instead of solid black. b7tree = b907098 + face.cpp + anim_test.cpp (FaceHairCoverage: brow 0.6-0.9, lash 0.35-0.62;
  b7 0.78-0.85 / 0.42-0.51; batch 6 values 0.98-0.99 / 0.54-0.64) + preview.cpp (per-card dither, coverage port).
  anim_test ALL PASSED, preview/carview compile+run. chain42: O2 build nt_b7_o2.exe, 30 portraits -> b7, perf_b7.
  Before = b6s36 (nt_b6_o2 + s36 shader override = main b907098's faces).
- 10:30: batch 7 O2 nt_b7_o2.exe, 30 portraits b7 (0 FATAL): 1 m day brows 0.7-2.3 stops below forehead (pre-s36
  levels); 0.5 m strand texture. Montages b7_c*.png, b7_brows_0p5m.png, b7_lashes_0p5m.png, b7_1m_day.png. Waiting:
  perf_b7 (chain42), hats (chain43: hats_b6s36 vs hats_b7), greet (chain44: greet_b7). Files staged /tmp/faces/batch7
  (face 76b7512e, anim_test 70e6dc61, preview c9f08c07). Draft msg /tmp/faces/msg/batch7_draft.txt.
- Candidate next: buzz cuts read as a shiny swim cap (shell + combed-hair KK band; dev14 has a FACES_BUZZ scalp-card
  prototype); renderer note for the shell highlight direction on buzz cuts.
- 11:20: hats b6s36 vs b7 (people 2, 4, 22, 23): identical hair under hats, 0 FATAL (montage b7_hats.png). First b7
  perf ran under heavy load (ai 1.53, veh 0.72, peds 6.49: all doubled) -> A/B in chain48: main b907098 O2
  (nt_m907_o2.exe, tree /tmp/faces/m907) vs b7, twice each (perf_m907_1/2, perf_b7_1/2), then greet_b7, then the
  buzz test (nt_d14.exe: buzz_off / buzz_on with FACES_BUZZ). Mesh counts b6 vs b7 identical (5 characters).

## WIND-DOWN STATE (2026-10-06, after batch 7's sign-off) — read this first when resuming

### Signed off
| Batch | Content | Status |
|---|---|---|
| 1-5 | eyes, lids, lashes, strand brows, skin data, ears, beards, stubble, hairlines, brow tint, cheek line, ... | merged |
| 6 | lateral orbital rims (no trench beside the eyes), lower lid margin normals; anim_test trench check | merged (PR #37) |
| 7 | brow cards 0.45x density, lashes 0.75x (tint unchanged); anim_test FaceHairCoverage; preview.cpp per-card dither + coverage port | SIGNED OFF on HEAD b49bc7f: blobs face.cpp 76b7512e, anim_test.cpp 70e6dc61, preview.cpp c9f08c07 (in the shared tree and /tmp/faces/batch7/; message /tmp/faces/msg/batch7.txt) |

Nothing of mine is running (no chains, games or builds).

### Unfinished work (not signed off; dev only, contains getenv knobs: NEVER ship as is)
- /tmp/faces/dev14 = b7tree + a buzz-cut scalp-card prototype in hair.cpp (buildScalpCards case HAIR_BUZZ, tunable
  FACES_BUZZ=len0,len1,w0,spacing,dens,stand; cards only when FACES_BUZZ is set). QUICK exe /tmp/faces/nt_d14.exe
  (built 11:08) — never shot in game. Shot list /tmp/faces/buzz.args (people 6, 27, 35: buzz cuts without hats).
- /tmp/faces/dev12: two-segment brow-ridge arch (FACES_ARCH2) — DROPPED (bulged the ridge forward at the joint).
- /tmp/faces/dev13: dev knobs FACES_BROWK=dens,tint,lash (used to pick batch 7) — superseded by batch 7.
- Private shader overrides (never shipped): /tmp/faces/shd_dbg/lighting.hlsl (debug views 30-34: lighting terms;
  based on the OLD pre-snapshot-36 lighting.hlsl), /tmp/faces/shd_fill (own bounce prototype, obsolete: snapshot 36
  has kLampBounce), /tmp/faces/shd_dither (obsolete: snapshot 36 has per-card dither), /tmp/faces/shd_s36 (copies of
  the renderer's snapshot 36 lighting/dynamic; now in main).

### Next steps, in order
1. Base on the then-current main (check that face.cpp = 76b7512e etc.; rebuild trees with git archive).
2. Buzz cuts read as a shiny "swim cap" (smooth 2.5 mm shell, no cards, a combed-hair KK highlight band; ~10% of
   people have HAIR_BUZZ). Try the dev14 prototype in game (QUICK build exists): EXE=/tmp/faces/nt_d14.exe, shots
   buzz_off (no env) vs buzz_on (FACES_BUZZ=0.007,0.011,0.011,0.011,0.6,0.4) with buzz.args. If it helps, make the
   constants permanent (no getenv), check the triangle budget (buildScalpCards caps ~3400 tris), anim_test, O2 build,
   30 portraits + buzz shots, perf A/B. Renderer note to send with it: shells have no strand direction for buzz cuts
   (hairDirect uses "down along the surface"); a matParam flag for isotropic short-hair shells could fix the band.
3. Other observations (lower priority): lips read as a uniform salmon lozenge at 0.5-1 m (upper lip could be a shade
   darker, corners shaded); ears bright/prominent at 1 m; the upper lid band looks puffy at 1 m by frontal sun
   (physically lit, but wide); young heavy faces look lumpy under the night lamp (cheek pads).
4. Renderer-owned, already reported to the lead: tonemap sharpening halo (post.cpp p3.z 0.35) outlines brows/lashes at
   1 m (passed to the night-lighting renderer agent).

### How to resume
- Process: ship verified batches to "main" starting "faces snapshot-safe" (files + blob hashes written with
  `git hash-object -w`, player-facing changelog, tests, montage paths). Never commit/push; never edit src/game/*;
  shaders/src/render belong to the renderer (send proposals).
- Proof per batch: anim_test (tree with -I src), preview/carview compile, O2 build (`OUT=... sh build.sh`), the 30
  standard portraits (`EXE=... TIMEOUT=3600 /tmp/faces/shoot2.sh NAME --viewer characters --protagonists --clip 0
  --settle 14 --viewerlamp $(cat final_day.args) $(cat final_night.args)`), montages
  (`sh /tmp/faces/make_montages2.sh BEFORE AFTER TAG "label" "label"`), perf (`/tmp/faces/perf.sh EXE TAG`; under
  load run main and the batch alternately, twice), hats (hats.args) when hair changes.
- Measures: scratchpad/faces/py/brows.py (day|night brow and socket stops at 1 m), sockets.py, chans.py, vprof3.py.
  Tools in /tmp/faces/tools/ (pupilprof, topslice, frontprof, eyedepth, ...): build from a tree with `-I src -I .`.
- One game and one build of mine at a time; host compiles only with tools/memfree.sh >= 2000; kill only my PIDs.
- 13:30: lead verified batch 7 (all three blobs against HEAD's batch-6 versions); it goes into snapshot 41 after
  snapshot 40 (city batch 10). The greet context run and the buzz test were cancelled for the wind-down (chain48 killed
  after its perf A/B). Final hand-back sent: "faces done, nothing running".
