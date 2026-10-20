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

## RESUMED 2026-10-12 (main = 894e7fb, batch 7 merged in PR #41)

- Disk cleanup done: /tmp/faces 2.1 GB -> ~55 MB (old exes, trees, portrait folders, test binaries deleted; montages
  in scratchpad/faces kept; b7/ portraits kept; prototype diff saved as /tmp/faces/buzz_proto.diff).
- Buzz-cut people in the viewer (no hats): 6, 27, 35, 67 (hairlist tool: /tmp/faces/hairlist, source
  /tmp/faces/tools/hairlist.cpp). Protagonists: Mari style 6, Dex style 2 (not buzz: their looks don't change).
- Trees: /tmp/faces/bz = clean main 894e7fb (the "before"); /tmp/faces/dev15 = main + buzz cards with DEV knobs
  (FACES_BUZZ=len0,len1,w0,spacing,dens,stand; FACES_BUZZ2=fadeCut,lenFade,hRoot,hTip; FACES_BUZZ3=fringeLen,
  shellSkin,colRoot,colTip; FACES_BUZZOFF=1 -> main's look). NEVER ship the knobs.
- Shot list /tmp/faces/buzz3.args (people 6,27,35,67 x 0.5/1/4 m x day/night; generator scratchpad/faces/py/mkargs.py).
  chain49: nt_d14 buzz_off vs buzz_on (prototype defaults). Hair crops: /tmp/faces/grid_hair.sh, montage_hair.sh.
- Host preview with knobs: /tmp/faces/pv15; /tmp/faces/pvbuzz.sh <bin> <tag> [view] [dist] (cards off/on grid).
- 14:45 prototype (nt_d14) in game: cards give a fuzzy silhouette + grain, but a bright "frosted" band at the front
  hairline (fringe cards at depth 0 + front cards catch the sun/lamp) and dark dashes on the top at night (cards'
  narrow KK lobe vs the LOD0 shell's broad sheen). Process fix (coordinator): every game launch goes through
  /tmp/faces/gate.sh (lead flag, free slot, memgate 3200) before tools/run.sh; builds gate.sh 2600.
- dev15 QUICK exe /tmp/faces/nt_d15.exe (15:02) has knobs FACES_BUZZ..BUZZ5 (BUZZ4=depth0,depth1,depthFringe,
  fringeDens; BUZZ5=flowJitter). Variants (chain50/51, people 6,27,35 x 0.5/1 m x day/night, buzzv.args):
  v1 BUZZ3=0.5,0,0.45,0.9 BUZZ4=5,3,4,0.6 -> no frost, fine grain by day, soft hairline (good); night top still has
  dark blotches on black hair. v2 = v1 + shellSkin 0.2; v3 = v1 + finer cards; v4 = BUZZ4=4,1,4,0.6 + jitter 0.8.
- GPU cost script /tmp/faces/perf_gpu.sh <exe> <tag> [env] (--synctimers, parser scratchpad/faces/py/passtimes.py).
- 16:30 main moved to 61a3b7b (MSVC CI: no GCC builtins / VLAs / __attribute__ / M_PI; src/anim unchanged).
  /tmp/faces/b8tree = 61a3b7b + knob-free buzz code (v1 values: cards len 7-11 mm, w 11, spacing 11, dens 0.6, NS 2,
  depth 5/3, colours 0.45/0.9, fade keep >= 0.3 and len x Lerp(0.5,1,keep), fringe len x0.5 dens x0.6 depth 4, shell
  mixed 20% toward the base skin before kCardShellShade) + anim_test BuzzCut (fails on main: no cards).
  /tmp/faces/m61 = clean 61a3b7b (O2 build -> nt_m61_o2.exe, c_build_m61.sh). Combined proof shot list
  /tmp/faces/fb_all.args (30 portraits + buzz 6/27/35/67 + hats 5/15/19/33; unique positions).
- 16:52 MISTAKE: killed PID 23313 + "sh orch12.sh" (locotour agent's) thinking it was my waiter; reported to the
  coordinator. Rule now: chains write PID files (/tmp/faces/*.pid), kill only those PIDs / their process groups.
- dev16 QUICK exe nt_d16.exe (knobs incl. FACES_BUZZ 7th/8th = NS, layers). chain52 (detached, pid file): v4 (jitter
  0.8, depth 4/1) and v7 (NS 1, small cards 4-6 mm, w 7, spacing 8, dens 0.5 + jitter).
- 18:00 CHOSEN = v7 (all three people: fine grain, no frost, no night blotches). b8tree hair.cpp = v7 exactly without
  knobs (blob f22c050f): cards NS 1, len 4-6 mm, w 7, spacing 8, dens 0.5, stand 0.4, depth 5/3, colours 0.45/0.9,
  flow jitter 0.8 (scalpFlow, own Rng per card), fade keep, fringe x0.5 len / x0.6 dens / depth 4; shell skin mix
  DROPPED (untested). anim_test b8 (blob b0a6b7e6) ALL PASSED: BuzzCut 2620-2730 card tris, <= 8.1 mm off the head,
  <= 1% over 4 mm (fails on main: 0 cards). preview + carview compile/run. cnt: buzz people +2.4..2.7k tris (hats
  +0.7..0.9k); c6/c27 hit the 36k LOD0 cap (body -346/-267 tris); non-buzz people identical.
- Proof chain: chain53 (fb_m61: m61 O2, 66 shots) -> chain54 (fb_b8 66 shots; perf_gpu m61/b8; perf m61/b8 x2).
  b8 O2 build c_build_b8.sh -> nt_b8_o2.exe. All detached with pid files in /tmp/faces/*.pid.
- Lips batch notes: face.cpp ~682 (lipCol/lipInner mix, lipMask), relief/shadeK at ~630 (white roll, corner depth),
  character.cpp ~688 (lipCol from skin). Preview framing for mouths: PREVIEW_TGT=0,0.08,-0.012 --dist 0.16.
- 19:05 fb_m61 (before) done: 66 shots, 0 FATAL/vkd3d. fb_b8 queued in chain54.
- NEXT BATCH PRIORITY (seen in fb_m61 at 1 m day): the eye region reads as "goggles" (brow line + shaded lid band +
  eyes as one horizontal bar, light nose bridge between). Profile tool /tmp/faces/eyeslice (source tools/eyeslice.cpp,
  SDF profile at x = eye.x, 5 mm grid): the upper lid is one convex dome ~11 mm tall from the margin (~4 mm above the
  eye centre) to a tight notch at the brow ridge (~15 mm above): face.cpp ~217 upper-lid ellipsoid z radius er*0.8 +
  lid mound er+0.9 mm, blends R(8 mm). Ideas: a smaller lid dome (z radius ~0.6 er) and a sulcus (subtractive
  ellipsoid under the brow ridge) so the crease/fold read, soft shading not a box. Do this before lips/ears (the
  coordinator listed lips, ears, upper lids; tell them the order change and why).

## 00:15 (2026-10-13): BATCH 8 SIGNED OFF (buzz cuts)
- Message /tmp/faces/msg/batch8.txt; files /tmp/faces/batch8/ (hair.cpp f22c050f, anim_test.cpp b0a6b7e6; blobs
  written with -w; copies in the shared tree). HEAD was c1af87a (my files unchanged since 61a3b7b). anim_test ALL PASSED
  on c1af87a/590854d/61a3b7b + files; BuzzCut fails on main (0 cards). fb_m61/fb_b8: 66 shots each, 0 FATAL.
  Perf: main 6.50 (spike)/3.75, b8 3.85/3.98 peds. GPU synctimers: noise +-15-30%, no visible cost.
  Montages scratchpad/faces/b8_*.png. Told the coordinator: next = upper lids (goggle look) before lips/ears.
- Deleted: dev14/15/16, bz, m61, nt_d14/15/16, nt_m61_o2.exe, variant shot folders (grids kept as
  scratchpad/faces/b8_variants_*.png).

## BATCH 9 (upper lids / eye region) — in progress
- dev17 = b8tree + knobs FACES_LID=zScale,fwdScale,fill,blend (face.cpp ~219: upper-lid ellipsoid z radius er*zScale,
  forward (up+0.3mm)*fwdScale, blend R(blend); fill > 0 adds an ellipsoid under the brow ridge at e+(0,12.5,13.5) mm,
  radii (13, 3.5*fill, 2.5) mm, R(6 mm)). QUICK exe /tmp/faces/nt_d17.exe. eyeprof tool (/tmp/faces/eyeprof17 with
  dev17): default profile has a 5.6 mm ledge under the brow at z 13-15 mm; fill 1 makes it a smooth slope.
- chain55: lv_L0 (default), lv_L1 (fill 1), lv_L2 (fill 1.5) with lidv.args (Mari, Dex, 7, 17, 27, 38 at 0.5/1 m day
  + 1 m night). Before = fb_b8 shots / nt_b8_o2.exe (61a3b7b + batch 8).
- 02:35 batch 8 accepted, in snapshot 46 (main 67e6159). Coordinator: eye region before lips OK.
- Eye-region findings (bar.py: stops below the forehead at the nose bridge / lids, 1 m; scratchpad/faces/py/bar.py):
  lid fill 1.5 lightens the lids 0.2-0.7 stops (N2/N3, L2); the nasion knob (carve moved forward 4/7 mm) barely
  changes the bridge (-0.1..-0.3). midprof (tools/midprof.cpp, p0/p1 = protagonists): between the brow heads the
  midline is a GROOVE 4-6 mm behind the medial brow (x=17 mm) at z +4..+14 mm above the eye centre: the dark notch =
  the goggles' bridge. FACES_GLA=1.5,3 (midline ellipsoid at N+(0,3,3) mm, radii (6.5, 4*amt, 7.5) mm, R 6 mm) levels
  it (Dex nasion depth 11.5 -> 6 mm; Mari 10 -> 5.4 mm).
- dev20 = 67e6159 + knobs (FACES_LID, FACES_NAS, FACES_GLA); QUICK nt_d20.exe; chain57: G0 default, G1 fill1.5+nas7+
  gla1.5, G2 fill1.5+nas4+gla1 (glav.args: Mari, Dex, 7, 38, 17, 27 at 0.5/1 m day + 1 m night).
- Deleted: dev17/18/19, nt_d17/19, lv_*, fb_m61, b7, pvb (variant grids are in scratchpad/faces/b9_*.png).
- 03:35 b9tree = 67e6159 + face.cpp (G1 values, no knobs: lid fill under the brow ridge e+(0,12.5,13.5) mm radii
  (13, 5.25, 2.5) R 6 mm; nasion carve centre 16.6 mm forward (was 9.6); glabella fill N+(0,3,3) mm radii
  (6.5, 6, 7.5) R 6 mm) + anim_test FaceShape checks (rise under the brow < 2 mm/mm, groove between the brows
  < 2 mm). Draft blobs face dd935b1a, anim_test 248cbf6a. anim_test ALL PASSED (rise <= 1.59, groove <= 1.2); on main
  the new checks fail (17 failures). O2 before nt_m46_o2.exe (67e6159), O2 after building (c_build_b9.sh ->
  nt_b9_o2.exe). Proof list /tmp/faces/fb9_all.args (48 shots: 30 standard + 3/4 views at 0.5 m day/night + Mari/Dex
  profiles at 1 m + c13/20/14/18 at 1 m day/night). Montages: /tmp/faces/make_montages9.sh B A labelB labelA.
- Coordinator hold on my game launches until 03:55 (/tmp/faces/hold_games read by gate.sh; delete the file to release).
- 05:10 holds: coordinator asked to hold game launches until 05:45 (/tmp/faces/hold_games; gate.sh now re-checks the
  hold every loop and after the memory wait). chain58 replaced by chain59 (fb9_b9 48 shots, then perf m46/b9 x2).
  fb9_m46 (before) done: 48 shots, 0 FATAL.
- Nasion check (midprof_b9 / midprof_m46): G1 depth 3.3-7.8 mm (main 7.1-12.9), min moves to z -2..0; the milder
  NAS 4 + GLA 1.5@6 keeps 4.3-9.3 mm, groove <= 1.3 (alternative if G1 reads too flat in the y90 profiles).
- LIPS dev: dev21 = b9tree + FACES_LIP=ageDesat,upperDark,borderSoft,cornerDark (face.cpp ~694 and paintFaceSkin
  corner); preview (pv21) liked 0.5,0.08,0.4,0.25 (older lips closer to skin, softer border). QUICK nt_d21.exe building.
- EARS: ears.py (scratchpad/faces/py) edge-based measure is unreliable with hair at the sides; c0/c1/c7 ears are
  0.3-0.6 stops darker than the cheeks by day, slightly redder (Dex). Low priority until a better measure.
- LESSON 06:00: editing /tmp/faces/gate.sh in place while a gate.sh was running made the running dash read garbage
  (syntax error after "gate passed"; harmless this time). Edit running scripts by writing a new file and mv-ing it.
- c14 and c18 wear sunglasses (bad picks for the eye region); pick extra people without glasses next time.

## WIND-DOWN STATE 2 (2026-10-13 ~07:30) — read this first when resuming

### Signed off / merged
| Batch | Content | Status |
|---|---|---|
| 1-7 | eyes, lids, lashes, brows, skin, ears, beards, hairlines, orbital rims, brow/lash densities | merged (7 in PR #41) |
| 8 | buzz cuts: short one-segment scalp cards, scattered flow, fade, short fringe; anim_test BuzzCut | merged in snapshot 46 (67e6159) |
| 9 | eye region "goggles": lid fill under the brow ridge, shallower nasion + glabella fill; FaceShape ledge/groove checks | SIGNED OFF on HEAD ab5995c: face.cpp dd935b1a, anim_test.cpp 248cbf6a (message /tmp/faces/msg/batch9.txt, files /tmp/faces/batch9/) |

### Parked (not signed off; dev only, contain getenv knobs: NEVER ship as is)
- LIPS: /tmp/faces/dev21 = batch 9 tree + FACES_LIP=ageDesat,upperDark,borderSoft,cornerDark (face.cpp ~694: the lip
  colour mix and the vermilion border's softness; paintFaceSkin ~1897: corner darkening). QUICK exe /tmp/faces/nt_d21.exe
  (built 05:27, has the knobs). Shot list /tmp/faces/lipv.args (Mari, Dex, 7, 17, 38, 20 at 0.5/1 m day + 1 m night).
  chain60.sh (P0 default, P1 0.5,0.08,0.4,0.25, P2 0.7,0.12,0.6,0.3) was cancelled before running. Preview liked P1:
  older lips closer to the skin, a softer border, upper lip a shade deeper, corners darker. Observed problem: lips read
  as a uniform salmon lozenge with a hard outline at 0.5-1 m (all ages the same saturation).
- EARS: only measured (scratchpad/faces/py/ears.py; unreliable where hair covers the head's sides). c0/c1/c7 ears
  0.3-0.6 stops darker than the cheeks by day; nothing built.

### Next steps, in order
1. Rebase on the then-current main (check face.cpp = dd935b1a once batch 9 is merged; rebuild trees with git archive).
2. Lips: shoot chain60's variants (rebuild dev21 on the new base first), pick, remove the knobs, usual proof.
3. Ears: a better measure first (preview albedo/lit ratios), then decide whether anything needs changing.
4. Renderer-owned, already reported: tonemap sharpening halo at 1 m (post.cpp p3.z 0.35).

### How to resume (process)
- Every game launch: /tmp/faces/shoot2.sh / perf.sh (both run /tmp/faces/gate.sh: coordinator holds via
  /tmp/faces/hold_games HHMM, lead flag, free slot, memgate 3200; then tools/run.sh). Builds: gate.sh 2600.
- Chains: write a PID file, run detached with setsid; kill only by my own PID files / process groups.
- Never edit a running script in place (write a new file and mv).
- Proof per batch: anim_test on HEAD + files, preview/carview compile+run, O2 before/after builds, standard 30 portraits
  + batch-specific shots (0 FATAL/vkd3d), montages, perf A/B twice (main/batch alternately), blobs with -w, copies in
  the shared tree (check the shared tree's copies equal HEAD's first).
- Tools: /tmp/faces/tools (midprof.cpp: midline/eye-column SDF profiles; hairlist; cardoff; fadelist; eyeslice ...),
  scratchpad/faces/py (bar.py goggle-bar stops, brows.py, ears.py, passtimes.py, imgdiff.py, mkargs.py shot lists
  with yaw), grid_eyes.sh / grid_ab.sh / montage scripts in /tmp/faces.

## PARKED (2026-10-13 ~08:45) — final state; read "WIND-DOWN STATE 2" above for the full picture
- Batch 9 SIGNED OFF and ACCEPTED for snapshot 48 (coordinator 08:4x): face.cpp dd935b1a, anim_test.cpp 248cbf6a, on
  HEAD ab5995c; copies in the shared tree and /tmp/faces/batch9/; message /tmp/faces/msg/batch9.txt. Perf (cpu ms peds):
  main 4.99/6.00, b9 4.92/5.95 (loaded machine). Glasses check (tools/glassclear.cpp): no new contact, fewer than main.
- Next, in order: (1) rebase (face.cpp dd935b1a once snapshot 48 merges); (2) LIPS: knob prototype = /tmp/faces/dev21
  (source; its knob diff vs batch 9 is /tmp/faces/lips_proto.diff; nt_d21.exe deleted: rebuild QUICK on the new base),
  shot list lipv.args, chain60.sh variants P0/P1/P2 (never run); (3) EARS: needs a better measure first.
- Kept for the batch 9 merge: nt_m46_o2.exe, nt_b9_o2.exe, fb9_m46/, fb9_b9/ (delete after snapshot 48 merges).
  Deleted: b8tree, b9tree, b9h, m46, dev20, variant shot folders, old exes and test binaries. /tmp/faces ~100 MB.
- Nothing of mine is running; /tmp/faces/hold_games removed; no pid files left in /tmp/faces.

## RESUMED 2026-10-19 (HEAD 0d67933; batch 9 merged af8a066 / v0.02.6)
- Deleted the batch 9 proof leftovers (nt_m46/b9 exes, fb9_* shots).
- LIPS (batch 10) in progress: /tmp/faces/dev22 = 0d67933 + knobs. Measure tool /tmp/faces/tools/lipstat.cpp (CIELAB
  of the vertex albedo: upper/lower lip, corners, chin, philtrum; build with /tmp/faces/c_lipstat.sh <tree> <bin>).
  Finding: lips vs chin skin dL -4..-16, da +7..+14, db -2..+2 -> too yellow ("salmon"); literature (lip - skin):
  dL ~-8..-13, da ~+8..+10, db ~-5..-7, lip hue ~25-30 deg. Knobs: FACES_LIP=ageFade,upperDark,borderSoft(x age),
  cornerDark; FACES_LIP2=redK,blueK (multiplies the vermilion colour). Candidate 0.35,0,0.4,0.25 + 0.96,1.22 gives
  db -5.5..-7.3, da +5..+12 (less for the old), dL -5..-13. F_LIP only where the border mask > 0.5 (a softened
  border's skin-side row stays skin; identical to main when the softness is 0).
- chain61: QUICK nt_d22.exe, variants P0 (main look), P1 (0.96,1.22), P2 (0.93,1.32) with lipv.args.
- 15:20 in game (chain61): P1 rose (LIP 0.35,0,0.4,0.25 + LIP2 0.96,1.22) clearly better than main's coral lips
  (Mari, 17, 20, 7); Dex a touch mauve but plausible. P2 cancelled. CHOSEN = P1.
- /tmp/faces/b10tree = 0d67933 + face.cpp (P1 values, no knobs: age-softened border 0.4*age, age fade 0.35*age toward
  skin*0.9, hue x(0.96,1,1.22), corner darkening 0.25, F_LIP where the border mask > 0.5) + anim_test LipColour
  (12 clean-shaven faces: da > 3, db < -3, dL < -3 on light/medium skin, 40 years older da at least 10% less; main
  fails 20 checks). chain62 (detached): O2 m0d (main) + b10, 40 shots each (fb10_all.args), perf x2.
  c_hostchecks10.sh: anim_test, preview/carview, cnt, lipstat main vs b10 -> /tmp/faces/hostchecks10.out.
- 18:00 batch 10 (lips) proof: anim_test ALL PASSED on 0d67933 and 731a731 (HEAD now; animator only) + files;
  LipColour fails on main's face.cpp (20 failures); preview/carview OK; triangles identical; fb10_m0d / fb10_b10
  40 shots each, 0 FATAL; montages scratchpad/faces/b10_*.png; lipstat table /tmp/faces/msg/lipstat_b10.txt.
  Staged /tmp/faces/batch10/ (face.cpp 29ae9767, anim_test.cpp 3067a353; not yet -w / not yet in the shared tree).
  Waiting: perf m0d/b10 x2 (chain63). Holds today: compile hold until the night d1 compile started (15:44), and
  hold_all 17:50 released at once (nightwalk already up).
- 21:00 batch 10: perf m0d run 2 = peds 4.17 (run 1 2.97; b10 run 1 4.01); b10 run 2 pending (chain63). Message body
  ready: /tmp/faces/msg/batch10_body.txt (PERFBLOCK placeholder). HEAD now 1a83e24 (src/game only since 731a731; my
  files still dd935b1a / 248cbf6a there and in the shared tree).
- EARS (batch 11 candidate): FOUND a real defect. The canal pit (relief canal -0.009 x1.75 + concha) goes ~23 mm along
  `out` and dips under the HEAD's surface, so the head's own skin shows through the canal as a pale disc ("white
  dot"/teardrop in preview albedo and lit; /tmp/faces/pvear/m.png, /tmp/faces/pvear2/wa3.png = wire+albedo). The cap
  centre also sits 13 mm above ring 0 (a spike: protrude - 0.011 predates the x1.75 relief scale).
  dev23 = 1a83e24 + batch 10 + DEV KNOB FACES_EAR=1 (keepOut: inner front rings s < 0.45 and the cap pushed along `out`
  until head sdf >= 1.5 mm; cap = ring-0 centroid - 0.5 mm) + FACES_EARDBG prints. Tool /tmp/faces/tools/earray.cpp
  (rays within 3.5 mm of the canal, 15 directions; % whose first hit is not the ear; ear/cheek albedo) built by
  /tmp/faces/c_earray.sh TREE BIN. chain64 (after chain63): 30 ear shots ears2.args with nt_b10_o2.exe -> ear_b10/.
- 21:40 BATCH 10 SIGNED OFF (message /tmp/faces/msg/batch10.txt; blobs -w 29ae9767 / 3067a353; copied into the
  shared tree after checking HEAD 1a83e24 = dd935b1a / 248cbf6a). Perf: main 2.97 / 4.17, b10 4.01 / 4.10 (offered a
  third pair). Keep nt_m0d_o2.exe, nt_b10_o2.exe, fb10_* until batch 10 merges.
- EAR measure (earray_d23 = dev23 build; FACES_EAR=0 is main): head skin through the canal 42.6% of rays (all
  people 40-45%); ring 0 (s .06) 8.4 mm inside the head sdf, rings .18/.30 +2.0/+0.9 mm. FACES_EAR=1 (keepOut 1.5 mm):
  5.7% (worst views yaw -40, from behind). Ear/cheek albedo 0.69-0.84 light/medium, 0.91-0.99 deep skin.
- 21:45 Coordinator: batch 10 ACCEPTED (snapshot 56, after 55). Follow-up asked: c1 (Dex, fair freckled man) lips
  mauve at 0.5/1 m day (confirmed in my grid; night fine; 17 the fairest is fine, so L* alone is the wrong trigger).
  dev23 knob FACES_LIPB: unset 1.22; h<deg> = lift no further than lip hue <deg> (labHueDeg on c.lipCol); L<k> =
  coordinator's Lerp(1.22, k, sstep(.25,.4,lum)); <k> constant. FACES_LIPDBG prints the lift.
- chain64 was WRONG (no --viewer flags: empty world shots); killed (own PGIDs) and replaced by chain65 (ear baseline
  with the viewer flags, then V0..V3 on nt_d23.exe: lipb.args 7 shots + earfix.args 10 profiles in V0).
  c_build_d23.sh (QUICK nt_d23.exe) waits for pv_d23; bvnf09nqz then builds earray_d23 + lipstat_d23 and runs them.
- 21:42 HOLD: coordinator asked no new games/compiles until PID 14554 gone or 22:30: hold_all=2230 +
  hold_release_14554.sh (removes it when 14554 exits).
- 22:25 Hold released 21:46 (14554 gone). nt_d23.exe built (QUICK, knobs EAR/LIPB). ear_b10/ = 30 baseline ear shots
  (0 FATAL): in game the pale canal blob IS visible at 0.5 m profile (Dex clearly, Mari subtly), faintly at 1 m.
- Ear tool per-view: main yaw 0 profile 22-32% head through the canal, yaw -40 (behind) 100%; fix (1.5 mm) 0% at yaw
  -20..+40; ~40% at yaw -40 (from behind: canal naturally hidden; CLR 2.5 -> ~25%). Knob 2 (all rings) = knob 1.
- Canal darkening (preview pv_d23b, FACES_EARCAN=0.35,0.2,0.25 chosen: ring x Lerp(0.35,1,sstep(.04,.25,s)), cap
  x0.2) makes the canal read as a dark opening; /tmp/faces/pvear4/canal.png.
- lipstat_d23 (lower lip hue): h28 -> Mari 29.6 / Dex 30.0 (db -3.9/-3.7), others unchanged; h31 -> 32.5/32.6
  (db -2.8, fails LipColour's -3); L1.12 hits all light skin (db up to -2.1 on 30, 17 hue 43).
- /tmp/faces/b11tree = dev23 base + batch10 face.cpp + CLEAN ear fix (clearOfHead 1.5 mm on front rings s<.45, cap at
  ring-0 centroid -0.5 mm, canal darkening, cap 0.2) + anim_test EarCanal (yaw -20..40, <2% per ear, cap <1.5 mm,
  canal < 0.6 of ear mean). Lip rule still to add after V1-V3 shots.
- 22:55 BATCH 11 candidate = /tmp/faces/b11tree (1a83e24 base; host tests) and /tmp/faces/b11g (0d67933 + b11 files;
  game exe, pairs with nt_b10_o2.exe). Contents: ear canal fix (clearOfHead 1.5 mm on front rings s<.45; cap at the
  ring-0 centroid; canal darkened Lerp(0.35,1,sstep(.04,.25,s)), cap x0.2) + lips lipBlueLift(lipCol, skin): hue floor
  28 deg x sstep(.15,.3,lum(skin)) (deep skin keeps the full lift; deep-skinned men 13/15/55/57 have hue ~24-27 and
  were hit by an unscaled floor). Tests: EarCanal (main 32 fails: 24-35% head through, 12-13 mm spikes) and LipColour
  pink-toned tones (Mari's, Dex's, rosier: hue >= 26, da > 3, db < -1; batch 10: 16-25 deg FAIL). anim_test (pre-fade
  version) ALL PASSED: pink hue >= 29.1, db <= -1.4; ears 0.33% (worst 1.3%).
- In game V0 (ear fix, no darkening): the pale canal blobs are gone (pvl3/canal_a.png). chain65 V1-V3 CANCELLED (slots
  full; the b11 proof shots show the rule). L1.12 (coordinator's literal) fails LipColour on 2 of 12 (db -2.1/-2.5).
- Queue: b1ezm42lg (lipstat_b11 + anim_test b11tree) -> c_build_b11.sh (O2, waits for ALL PASSED) -> chain66 (47
  shots fb11_b10/fb11_b11 with fb11_all.args, perf b10x/b11 x2).
- 23:05 Coordinator approved the batch 11 plan; TRIMMED proof: no perf pairs (state the per-person triangle-count delta
  instead); shots = mouths of Mari, Dex, 17 + two deep-skin people (27, 57) at 0.5 m day / 1 m night, ears of the six
  standard people at 0.5 m day profile (fb11_trim.args, 16 shots). chain66 cancelled -> chain67. AFTER SIGN-OFF: PARK
  (resume notes, launch nothing; my slot goes to the hand-built world's stage 0).

## PARKED (2026-10-20 ~00:40) — read this first when resuming
- Coordinator: after batch 11, PARK and launch nothing; my slot went to the hand-built world's stage 0. Faces work can
  resume later.
- Batch 10 (lips rose): MERGED (PR #49, 0c288db). Batch 11 SIGNED OFF on HEAD 2b3102e (message
  /tmp/faces/msg/batch11.txt): face.cpp c8f604e3, anim_test.cpp c96d9d4c (blobs -w; copies in the shared tree and
  /tmp/faces/batch11/). Contents: ear canal clear of the head (1.5 mm, cap at the floor, canal darkened) + lip blue
  lift stops at hue 28 deg on fair skin (faded in over skin lum 0.15-0.3). Tests EarCanal + LipColour pink tones.
  anim_test ALL PASSED on 2b3102e, 0c288db and 1a83e24 + files; preview/carview OK; triangles identical (8 people);
  16 shots each fb11_b10 / fb11_b11 (0 FATAL); no perf (coordinator: skip). Montages scratchpad/faces/b11_*.png.
- Kept until batch 11 merges: nt_b10_o2.exe, nt_b11_o2.exe, fb11_b10/, fb11_b11/, b11tree, b11g, b10tree. Dev
  prototype with knobs: /tmp/faces/dev23 (FACES_EAR/EARCLR/EARCAN/LIPB/LIPDBG/EARDBG; never ship) and pv_d23b.
  Tools: /tmp/faces/tools/earray.cpp (ear sight-line tool; EARRAY_MAP=yi*10+pi ASCII maps), lipstat.cpp, grid_ears.sh.
- Open / ideas for later (not started):
  - ear tone: ear/cheek albedo 0.69-0.84 on light skin (fold darkening baked into albedo: flat 0.9, concha 0.61);
    in game ears read a bit redder/darker than the face; plausible, left alone.
  - from 40 degrees behind the ear the head still covers ~40% of the canal patch (natural occlusion, not a defect).
  - 27's concha catches a small sun highlight (sheen), slightly brighter after batch 11.
  - upper lids (from the old list) never started.
- Nothing of mine running, no PID files, no holds (/tmp/faces/hold_*), /tmp/faces ~170 MB.
