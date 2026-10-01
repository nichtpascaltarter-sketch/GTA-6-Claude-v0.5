# City blocks agent: resume notes (wrapped up 2026-10-01 ~21:20 UTC)

`$S` = `/tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad`
Repo: `/home/user/GTA-6-Claude-v0.5`, branch `claude/neon-tide`. Do not commit or push.

The task is the scorecard item "massing between landmarks still repeats". Ordinary blocks get district palettes of real
building types inside fixed envelopes. Lots, roads, anchors, interiors, doors and spawns stay where they are.

My files (src/world): buildings.h, buildings.cpp, buildmesh.cpp, facadedetail.cpp, cellgen.cpp, plus the new
blockstyle.cpp (included at the end of buildings.cpp) and massing.cpp (included by buildmesh.cpp). Ask the lead before
touching anything else. Never edit src/game, src/anim, src/render, src/shaders or src/gfx.

## What is merged

- Batches 1+2 are snapshot 23, merged to main as PR #23 (merge `6780edd`). The shared tree's 7 world files equal HEAD:
  buildings.h af7411f, buildings.cpp 8eb43d4, buildmesh.cpp 31aad38, facadedetail.cpp 2088f1f, cellgen.cpp f43ee69,
  blockstyle.cpp f7adc46, massing.cpp 6cea14e.
- What they did: about 35 district types, carved footprints, roof forms, district colour, street-level detail, block
  infill (back buildings with style BS_GARAGE, rear parking, yards), tower looks, and `BuildingSet::pavedAt` /
  `pavedGrid` for the renderer. Repetition (log metric) went from 13.7% to 4.3%. Story regression 27/27 passed.

## Batch 3: not signed off, not in the shared tree

- **Private copy: `$S/b3`** (`$S/dev3` is identical in src/world). It is the old pre-6780edd tree plus batch 3. The lead
  says the world files can be copied straight onto 6780edd: no other agent touched src/world, and buildings.cpp and
  buildmesh.cpp equal main.
- Changed vs main: buildings.h 1cb05fb, facadedetail.cpp 3e33935, cellgen.cpp e157f83, blockstyle.cpp 72266dd,
  massing.cpp 97ecbdb (buildings.cpp 8eb43d4 and buildmesh.cpp 31aad38 are unchanged).
- Contents:
  - motels in 4 types (MiMo motor court, Mediterranean, Keys, 70s motor inn with a mansard);
  - strip malls in 3 types (mission, MiMo folded plate, power centre);
  - farms: cracker / I-house / ranch farmhouses, and gambrel, pole and gable barns;
  - neighbourhood churches in 4 types (mission, clapboard steeple, brick Gothic, A-frame);
  - a far-LOD diet: porches, posts, rails, stairs and folded plates are drawn in the near LOD only.
  - Changelog draft: `$S/city/msg/changelog_b3.txt`. Snapshot message draft: `$S/city/msg/snapshot_b3_draft.txt`
    (placeholders `@REG@`, `@WC@`, `@TRIS@`, `@VIEWS@`, `@BLOBS@`).
- **Verified:**
  - b3 exe built (`/tmp/city/nt_b3.exe`, old base).
  - In-game check shots of every new type, inspected: `$S/city/game_b3check/*.png`, sheets in
    `$S/city/sheets/game_b3_new_types/`.
  - Clay before/after: `$S/city/sheets/clay_new_types/`.
  - worldcheck passed: `$S/wc/b3.txt`.
  - Story places identical to before (placescmp).
  - Tour stops 2-5 (draws mean/max, base2 → b3): 763/839 → 773/847, 630/645 → 629/646, 462/529 → 459/535,
    748/789 → 652/671. Sheet: `$S/city/sheets/tour_b3/tour_stops_2_5.png`.
  - Far-LOD tris for the whole map: 597k (main) → 1.22M (b2) → 1.01M (b3).
  - Repetition, log metric: 4.3% → 3.9%. Form twins: 3.8% → 3.2%.
  - Logs: `$S/city/logs/{tour_b3_log,clab_b3_final,clab_b3_perf}.txt`.
- **Not verified:**
  - Story regression on b3. It was stopped at wrap-up after 1 of 27 missions (low_tide passed). Partial log:
    `$S/city/logs/reg_b3_partial_log.txt`.
  - The 20 in-game district views for b3 were never shot.
- Known issue, already shipped in b2: the restyled tower at tour stop 3 shows darker storefront glass. The facade shader
  multiplies the shop interior by the facade's single glass tint. A shader proposal for the lead is in the snapshot draft
  (facade.hlsli ~l.359: `glassTint = storefront ? lerp(glassC, float3(0.86,0.9,0.92), 0.6) : glassC`).

### To sign off batch 3

1. Run the story regression on b3. Better: rebuild it on 6780edd first. `$S/base4` is exactly 6780edd plus the b3
   world files, so build `OUT=/tmp/city/nt_base4.exe sh ./build.sh` in `$S/base4`. Then:
   `TIMEOUT=9000 EXE=/tmp/city/nt_base4.exe WINEPREFIX=/tmp/wine_city nice -n 6 tools/run.sh --play --missiontest story --renderevery 300 --width 960 --height 540 --shotdir 'Z:\tmp\city\reg_b3\'`
   Run it from the repo root and copy the Wine log (`/tmp/wine_city/drive_c/users/root/AppData/Local/NeonTide/log.txt`).
   Compare with `python3 $S/city/tools/regcmp.py $S/city/logs/reg_b2_log.txt <new log>`. b2 passed 27/27.
2. Optional: the 20 views with `$(cat $S/city/jobs/base_shot_args.txt)` and `--settle 8 --shotdir ...`. Sheets via
   `python3 $S/city/make_sheets.py $S/city/game_base <after dir> <out dir> "tag"`.
3. Check that the shared copies still equal HEAD (hashes above). Copy b3's 7 world files into the shared tree. Run the
   MinGW syntax check in the shared tree (`x86_64-w64-mingw32-g++-posix -std=c++17 -fsyntax-only -Ibuild/gen -Isrc src/main.cpp`,
   with memfree ≥ 2000). Then send "city snapshot-safe ..." to main, filling in the draft.

## Batch 4: work in progress, not built into an exe yet

- **Private copy: `$S/dev4`** = main 6780edd plus the batch-4 world files. It passed the MinGW syntax check on
  6780edd (`/tmp/city/syntax_dev4.log`, 0 errors). The native citylab build is fine too.
- **`$S/base4`** = 6780edd plus the b3 world files: the "before" for batch 4. **`$S/m4`** = a pure 6780edd export.
- dev4 blobs: buildings.h f518689, buildings.cpp 8eb43d4, buildmesh.cpp 3255be5, facadedetail.cpp 53ef831, cellgen.cpp e157f83, blockstyle.cpp 2f2c76d, massing.cpp c70f05f.
- Contents:
  - **Yard trees** (buildmesh.cpp): the spots are now measured from the house, not from the lot centre. Previously they
    landed inside the house when the house sits forward of its lot centre. In b3, 5,357 of 47,580 yard trees stood
    inside building footprints. A per-building keep-out list (`YardKeep` in Ctx, `yardKeep()` / `yardKept()`) is filled
    by porches, deck steps, stilt-house decks, stairs and cistern, the colonial portico, pool deck, garage, carport and
    driveways. A blocked tree tries the mirrored spot, then the other yard, else it is dropped. Result with the final dev4
    code (citylab rebuilt 21:14): 47,152 trees, 0 inside buildings.
  - **Strip-mall and gas-station forecourts follow the ground** (`stripCourt()` in buildmesh.cpp). This replaces the
    slab at baseZ-0.1 with its concrete skirt. The slab stood 0.35 m above the ground on average, so parked cars and
    shoppers sank into it. Along the shopfronts there is a 1 m walk with a curb (median 0.29 m) and a collider where the
    step is ≤ 0.5 m (9 of 834 courts have none). Stall lines, canopy supports and gas columns/pumps stand on the asphalt.
  - **New house types:**
    - `AR_HOUSE_CBS` (52): 1950s Florida block house. Pastel stucco, low hip roof (white shingle, tile or white metal),
      aluminium window awnings on 70% (half of them striped), a carport.
    - `AR_HOUSE_VICTORIAN` (53): folk Victorian. Two-storey cross gable, an L-plan porch with brackets, a king-post
      gable truss and attic window, a two-strip driveway with no garage.
    - Palettes: CBS in North City / Calle Luna / Flats / Midtown, Westbrook, the small towns and the Keys. Victorian in
      Okahatchee / Harlow / Fort Castell, Key Solano and the farmhouses.
  - **Carports** replace the garage wing on CBS and MiMo houses: a flat slab on steel posts, a utility room at the back,
    and on half the MiMo houses a fin screen.
  - **Tile roof blends** (houseRoofFinish): terracotta 55%, brown blend 25%, aged dark red 20%.
- Twins report (dev4, `/tmp/city/dev4_report.txt` → `$S/city/logs/dev4_report.txt`): Westbrook's biggest twin group is
  still the ranch box. The small towns' twins are mostly bungalow boxes. Key Coral's twins are plain AR_NONE villas
  (the villa palette keeps AR_NONE at weight 2). Next ideas: more bungalow / ranch massing variants, and lower AR_NONE
  in villas.

### Next steps for batch 4

1. Rebuild citylab:
   `cd $S/citylab && g++ -std=c++17 -O2 -DCITYLAB_ARCH -I$S/dev4 citylab.cpp -o citylab_dev4 -lpthread`
   Then run from the repo root:
   `$S/citylab/citylab_dev4 --yardtrees --courts --stats --farbreak --find 52,2495,4012 --find 53,144,6475,1 --dump 3350,-843,45`.
   Check that yard trees are 0 inside buildings and that their count is close to b3's.
2. Clay views of the new types: write `views_b4.txt` with "front <building index> ..." lines from the `--find` output,
   and run `citylab_dev4 --views views_b4.txt $S/city/clay_b4`. Make before/after sheets against citylab_b3 with
   `$S/city/pair_sheet.py`.
3. Build `nt_dev4.exe` and `nt_base4.exe` (build.sh with OUT=, one build at a time, memfree ≥ 3000). Then run in-game:
   check shots (CBS, Victorian, carport, strip court with parked cars), tour stops 2-5 for both, story regression on
   dev4, and the 20 views.

## Tools

- citylab (native world generator, metrics and clay renders): `$S/citylab/citylab.cpp`. Binaries: `citylab_b3`,
  `citylab_dev4`, `citylab_base2` (old main world).
  - Build: `g++ -std=c++17 -O2 -DCITYLAB_ARCH -I<private root> citylab.cpp -o citylab_X -lpthread`. Run from the repo root.
  - Modes: `--stats` / `--logmetric` (repetition), `--farbreak`, `--tris x,y,rNear,rFar,label`, `--views list dir`,
    `--plan x,y,half,out.ppm`, `--find arch,x,y[,massing]`, `--dump x,y,r`, `--pavedtest`, `--yardtrees`, `--courts`,
    `--twins r1,r2,..` (Region ids: Westbrook 13, Okahatchee 18, Harlow 19, Key Solano 23, Key Coral 8, Ten Palms 16).
  - Perf script: `$S/city/tools/clab_perf.sh <citylab bin> <out>`.
- worldcheck: `$S/wc/worldcheck.cpp`. Build with `g++ -std=c++17 -O2 -I<root> worldcheck.cpp -o wc_X -lpthread`, run
  `wc_X 12` from the repo root. Results in `$S/wc/*.txt` (b3 passed).
- Comparison tools in `$S/city/tools/`:
  - `tourcmp.py <before log> <after log>`
  - `regcmp.py <before> <after>`
  - `placescmp.sh <log> <log>`
- Job scripts (detached runners): `$S/city/jobs/`. Use `nohup setsid` to get past the 2 h background limit, and wait
  for `sh tools/memfree.sh` ≥ 2500 before compiles and citylab runs.
- Sheets: `$S/city/make_sheets.py` (district views), `$S/city/pair_sheet.py` (before/after rows).

## Evidence and logs

- Sheets: `$S/city/sheets/`
  - `clay`, `clay_b2`, `game_b1`: batches 1-2 district views;
  - `clay_new_types`, `game_b3_new_types`, `tour_b3`: batch 3;
  - `tour_b2`.
- In-game baseline views: `$S/city/game_base/*.png` (20 views of old main).
- Logs copied from /tmp/city: `$S/city/logs/`.
  - tour: tour_base2_log, tour_b2_log, tour_b3_log;
  - regression: reg_b2_log (27/27);
  - citylab: clab_*, farbreak_*;
  - yard trees: yardtrees_b3 / yardtrees_dev4;
  - dev4_report;
  - job status files.
- Exes in /tmp/city: nt_base2.exe (old main), nt_b2.exe, nt_b3.exe. They are rebuildable and not in the scratchpad.

## Rules to remember

- Use only build.sh / tools/run.sh. Run at most one game and one build of mine at a time.
- Wait for memfree ≥ 2000 (2500 to be safe) before compiling.
- Kill only PIDs I started; never `pkill -f` broadly.
- Never use `near`, `far`, `min`, `max`, `small`, `interface`, `IN`, `OUT`, `ERROR`, `DELETE` or `TRANSPARENT` as
  identifiers.
- Run the MinGW syntax check before anything reaches the shared tree.
- Tell the lead before touching propmesh.cpp, splat or overhead maps, or new non-vegetation props.
