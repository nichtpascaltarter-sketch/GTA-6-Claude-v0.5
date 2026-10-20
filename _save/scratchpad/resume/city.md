# PARKED 2026-10-20 ~17:55 UTC (the lead: user's usage almost gone). Read this first; the older notes follow.

- Nothing of mine runs. I killed cq70 and its procedural drive (23901, 8195, 8210, 8239), the waiters, and cq71/cq71a.
- Done:
  - The roads set is in snapshot 63: tree 77c89be9, with main.cpp 5bde1642 and build.sh ff1e292c.
  - worldv's 4 signal.node warnings are settled as expected: n0003/4/6 are pilot-edge T's, n0039 is a real T.
- Part 1, mid-way. It must be signed off on 63, not 62.
  - $S/p63 = 63's tree ($S/s63, an archive of 77c89be9) + part 1. Only app.cpp changed in the rebase: a0fbda2e = part 1 merged cleanly onto 63's a4c4aee9 (merge files in $S/mrg63).
  - The other 20 blobs are unchanged; see /tmp/city/p1_blobs.txt. Write a0fbda2e with `git hash-object -w`, guarded on a4c4aee9, and replace the app.cpp line (it was 4e191ac8 on 30f8a386).
  - p63's world/core sources are byte-identical to p62's. So cq70's results on p62 carry over: CI, worldv 0/0, worldc/rbcheck IDENTICAL, census 0/55086, hand shots, hand drive (rc 0, 0 fatal, peds 88, veh 54).
  - Still to run on p63: CI update_logic_test (63 changed the updater), MinGW main.cpp, the exe, hand shots, a hand drive and a procedural drive.
  - Then fill /tmp/city/msg/part1_signoff_draft.txt: base 63, app.cpp a0fbda2e on a4c4aee9, @GAMES@.
  - Say in the sign-off: hand mode skips the main menu and setup page at startup, but the pause menu's Quit to menu still opens the main menu (saves are off there). Offer a guard if the lead wants one.
- Fixtures, ready but not tested:
  - $S/p62f is now 63 + part 1 + worldv's 3 fixes (transit.ntw, world_format_test.cpp, model roads.ntw, worldmodel_test.cpp, NTW_FORMAT.md).
  - Run CI's world_format_test, worldv_test, worldmodel_test and worldv on world/. Then blobs and the sign-off.
- Part 2a, written, never compiled:
  - $S/p2 = p62f + 2a. The desk check found nothing so far.
  - One fix to make: the Pad::z comment in worldmodel.h says "absolute", but the code treats z as above the grade.
  - /tmp/city/cq71a.sh (syntax checks + p62f tests) and /tmp/city/cq71.sh (2a's full chain) are written but not running. Re-point them before use; they assume cq70.
- Next: p63 checks → part 1 sign-off → fixtures sign-off → 2a compile, fix and checks → 2b/2c.

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

## Update 2026-10-05 (resumed on main 06580c3)

- `$S/base4` and `$S/dev4` were rebuilt as exports of 06580c3 plus the b3 / dev4 world files. The 6780edd-based dev4
  is kept in `$S/dev4_6780`; `$S/m5` is a plain 06580c3 export. src/world is unchanged between 6780edd and 06580c3.
- `/tmp/city/nt_base4.exe` (batch 3 on 06580c3) was built at 12:23. Job `/tmp/city/jobsX.sh` runs the story
  regression (log `/tmp/city/reg_base4_log.txt`), then the 20 views (`/tmp/city/base4_shots/`).
  - Status: `/tmp/city/jobsX.status`.
  - After it: `/tmp/city/views_b3_sheets.sh` (sheets), then `/tmp/city/snap_b3.sh` (copy into the shared tree and the
    syntax check), then send "city snapshot-safe".
- dev4 additions since the wrap-up:
  - forecourt plateau where a building is lifted above the ground (`stripCourt` returns the plateau height;
    `courtFoot()`);
  - front-bay ranch and block houses (MK_STEP_FRONT in buildArchHouseBody);
  - L bungalows with the porch in the corner;
  - fewer plain AR_NONE villas on Key Coral and the bay islands;
  - door wall choice for these in facadedetail.cpp.
- dev4 checks so far:
  - MinGW syntax OK on 06580c3.
  - citylab: form twins 2.6% (b3 3.2%), log metric 2.9% (b3 3.9%), far LOD 1.04 M (b3 1.01 M), yard trees 47,235 with
    0 inside buildings (`/tmp/city/dev4_report2.txt`).
  - Clay sheets: `$S/city/sheets/clay_b4/`.
- `/tmp/city/jobsY.sh` builds nt_dev4.exe. Once the batch-3 runs are done, `/tmp/city/jobsZ.sh` runs on dev4 the check
  shots (`/tmp/city/dev4_check_args.txt`) plus the 20 views, then the regression, then the tours (base4 and dev4).
  Status files: `/tmp/city/jobsY.status`, `/tmp/city/jobsZ.status`.
- Changelog draft for batch 4: `/tmp/city/msg/changelog_b4.txt`.

## Update 2026-10-05 ~15:50

- Batch 3 is merged: PR #27, main 8ee5699. Its world blobs are 1cb05fb / 8eb43d4 / 31aad38 / 3e33935 / e157f83 /
  72266dd / 97ecbdb. It passed 27/27 on 06580c3; the views are in `$S/city/sheets/game_b3/`.
- **Batch 4** is now `$S/dev4` (8ee5699 + the dev4 world files; the 06580c3 copy is in `$S/dev4_0658`).
  - Job `/tmp/city/jobsB.sh` (status `/tmp/city/jobsB.status`):
    - builds `/tmp/city/nt_dev4b.exe` (dev4 on 8ee5699) and `/tmp/city/nt_m6.exe` (main 8ee5699, the "before");
    - waits for the dev4 shot run (PID 26990, 06580c3 exe; the world is identical) to finish;
    - then runs the regression on dev4b, the m6 tour and the dev4b tour.
  - Shots go to `/tmp/city/dev4_shots/`: 20 check shots + 20 views.
  - After the runs: sheets, then copy dev4's 7 files into the shared tree (after checking they equal HEAD), the MinGW
    check and "city snapshot-safe" (draft `/tmp/city/msg/snapshot_b4_draft.txt`, changelog `.../changelog_b4.txt`).
- **Batch 5 (work in progress)** is in `$S/dev5` (8ee5699 + dev5 world files):
  - gas stations: 4 canopy kinds, pump islands, a price sign, the kiosk dressed by kind (blockstyle);
  - raised Keys houses: `AR_HOUSE_RAISED` = 54, built with buildStiltShack, `stiltFloorH()`;
  - stronger tile blends and white ranch roofs.
  - Clay sheets: `$S/city/sheets/clay_b4/{gas_stations,raised_houses}.png`. citylab binary: `citylab_dev5`.
  - Changelog draft: `/tmp/city/msg/changelog_b5.txt`.

## Update 2026-10-05 ~17:15

- The batch-5 work (gas stations, raised Keys houses, roof blends) is folded into batch 4:
  - `$S/dev4` world files = `$S/dev5` world files;
  - batch-4-only copy: `$S/dev4_b4only/src/world`.
- Job `/tmp/city/jobsD.sh` (status `/tmp/city/jobsD.status`) runs, in order:
  - MinGW check, then build `nt_dev4b.exe`;
  - regression;
  - extra shots (`/tmp/city/dev4c_shot_args.txt` into `/tmp/city/dev4c_shots`);
  - m6 tour, then dev4b tour.
- Done already: dev4 shots `/tmp/city/dev4_shots` (37 of 40, from the 06580c3 exe; the world is identical), sheets
  `$S/city/sheets/game_b4*`, worldcheck dev4 passed (`$S/wc/dev4.txt`).
- Then: `/tmp/city/snap_b4.sh` (copy into the shared tree + syntax check) and "city snapshot-safe" (changelog
  `/tmp/city/msg/changelog_b4_full.txt`).

## Update 2026-10-05 ~18:30

- Batch 4 (folded b4+b5, `$S/dev4`) checks so far: worldcheck passed on the folded files (`$S/wc/dev4c.txt`);
  citylab rebuilt on the final files (`citylab_dev4c`): log metric 2.9%, form twins 2.6%, far LOD 1,053,740 tris, yard
  trees 47,247 with 0 inside, courts 834 (curb median 0.29 m, 9 plateau). Perf: `/tmp/city/clab_dev4c_perf.txt`
  (tour stops 2-5 near rings unchanged: 116k/79k/223k/163k). Twins: `/tmp/city/clab_dev4c_twins.txt`.
- jobsD: the regression on nt_dev4b.exe started for real ~17:50 (it waited for a game slot). Then extra shots, tours.
- `$S/dev4/tools/memfree.sh` synced with the shared tree's edited copy.
- **Batch 6 (in progress) is `$S/dev6`** (= dev4 + yard work). Content so far:
  - pools that show: `backyardPool()` in buildmesh.cpp (deck on the highest corner + skirt; the lawn is cut away under
    it via `lawnHole()` / `Ctx::holes` → `buildFacadeDetail(..., lawnHoles)` → `lawn()`); the water was below the lawn
    and the terrain before, so pools read as grass squares. Shapes (rect / rounded / lap), coping, decks (pavers,
    keystone, concrete, wood in the Keys), spa on some, water = MAT_EMISSIVE param 6 turquoise (night glow on 65%).
  - driveway surfaces by district: `drivewaySurface()` (concrete, pavers, old asphalt, gravel/shell); lawn holes under
    driveways and the carport floor.
  - front boundaries by district: `frontBoundaryLook()` + `fenceRun()` in facadedetail.cpp (knee wall, reja, picket,
    coral rock, ranch rail, aluminium; legacy hedge / villa garden wall otherwise; interior hosts keep the legacy).
  - mailboxes: post, stucco pillar with a slot, black box on a steel post (country).
  - Views list: `$S/citylab/views_b6.txt`. Job `/tmp/city/clab6.sh` (status `/tmp/city/clab6.status`) builds
    `citylab_dev6` and renders clay views into `$S/city/clay_b6` (+ before with citylab_dev4c in `clay_b6_before`).

## Update 2026-10-05 ~18:50: batch 4 signed off

- Batch 4 (folded b4+b5) passed: regression 27/27 (0 changed vs b3), places identical, worldcheck passed, MinGW
  check in the shared tree 0 errors. **Copied into the shared tree** (main is now e28a7db; src/world unchanged since
  8ee5699). Blobs: `/tmp/city/b4_blobs.txt`. "city snapshot-safe" for batch 4 SENT (text:
  `/tmp/city/msg/snapshot_b4_final.txt`).
- Still owed to the lead: tour numbers (jobsD: m6 vs dev4b tours) and the in-game shot sheets
  (`/tmp/city/dev4c_shots`). Compare tours with `python3 /tmp/city/tools/tourcmp.py /tmp/city/tour_m6_log.txt
  /tmp/city/tour_dev4b_log.txt`.
- **Next batch is called "batch 5 (yards)" to the lead** (internally `$S/dev6`). Added since the notes above:
  back-yard fences (`backYardFence()` / `backRun()`, houses without hedges, by district), front walks (garden start,
  with lawn holes and yard trees moved aside), far-LOD pools as a plain rectangle.
  - Clay sheets: `$S/city/sheets/clay_b6/{pools_air,front_yards}.png`.
  - citylab dev6: far LOD 1.054 M -> 1.071 M; yard trees 47,244 (0 inside); near rings Westbrook 782k -> 828k,
    Calle Luna 446k -> 458k, North City 334k -> 355k, Key Solano 474k -> 496k; tour stops near unchanged.
  - Jobs: `/tmp/city/jobs6.sh` (MinGW check + nt_dev6.exe; status `/tmp/city/jobs6.status`), then
    `/tmp/city/jobs6b.sh` (after jobsD: dev6 shots + the same shots on dev4b as "before"; args
    `/tmp/city/dev6_shot_args.txt`; status `/tmp/city/jobs6b.status`).

## Update 2026-10-05 ~19:55: batch 4 merged (PR #30, main 111efa9); batch 5 (yards) now in `$S/dev7`

- `$S/dev7` = export of 111efa9 + the batch-5 world files (identical to `$S/dev6/src/world`). **Edit dev7 from now on**
  (citylab scripts point at dev7). main's src/world = batch 4 = `$S/dev4/src/world`.
- Batch 5 additions since the last note: paved front yards (`blockstyle::frontPaved`), the pool decision + fit moved to
  `blockstyle::poolDeck` (shared with `pavedRects` so the renderer's ground cover and the scattered vegetation skip pool
  decks and paved yards), scattered vegetation skips `pavedAt` (a palm grew through a gas canopy on main), the garden
  has its own vertex budget (34% of CBS houses had lost their whole garden to the awnings: fixed, lawn 100% now).
- citylab: `--frontkinds` and `--gardens` modes (build with `-DCITYLAB_YARDS`, binary `citylab_dev6y`).
- Job `/tmp/city/jobs7.sh` (status `/tmp/city/jobs7.status`): MinGW check + nt_dev7.exe, then after jobsD the 22 yard
  shots (`/tmp/city/dev7_shot_args.txt`) on dev7 (`/tmp/city/dev7_shots`) and dev4b (`/tmp/city/dev7_before_shots`),
  then the regression (`/tmp/city/reg_dev7_log.txt`).
- Changelog draft: `/tmp/city/msg/changelog_b5_yards.txt` (add: paved front yards, garden budget fix, palm fix).
- Owed to the lead for batch 4: tours (jobsD) + `$S/city/sheets/game_b4_new_types/gas_stations_raised_ingame.png`.

## Update 2026-10-05 ~20:50

- dev7 (batch 5) is FROZEN for testing: final edits were the district pool shares (blockstyle poolDeck), cheaper back
  fences / rejas / aluminium, and merged fence colliders (one box per level stretch). jobs7 restarted 20:47 (syntax,
  build nt_dev7.exe, after jobsD: shots, before shots, regression). worldcheck rerun in `/tmp/city/pre7b.sh` →
  `$S/wc/dev7.txt` (the earlier run passed with 782,975 colliders vs 595,482 on main, before the merge).
- Final dev7 citylab: far LOD 1.070 M; near rings Westbrook 864k, Calle Luna 464k, North City 358k, Key Solano 495k;
  per-house near verts +22.6% (CBS +46% = gardens restored); yard trees 47,087.
- `$S/dev8` = copy of dev7 for batch 6 (Keys/Ten Palms variety: Keys motels, conch types, stilt houses) - not started.

## Update 2026-10-05 ~21:00: batch 6 started in `$S/dev8` (= dev7 + edits)

- Keys motels: tin hip roof on 40% (blockstyle sets b.roof = ROOF_HIP; massing RFM_METAL_GABLE case reads it for
  BS_MOTEL), gallery rails painted (white 45%, else turquoise / sea green / coral / butter yellow) in MAT_PLASTER, white
  posts.
- Painted porch trim in town: `Ctx::trimMat` + `trimWood(x)` (buildmesh.cpp); buildArchHouseBody sets it (plaster) for
  BS_HOUSE/BS_VILLA; farmhouses, stilt shacks and raised houses keep bare wood. Used by housePorch, porchBrackets,
  galleryPorch, gableTruss.
- Eyebrow conch houses: `eyebrowPent()` across the front of 60% of two-storey box conch houses without the double
  gallery.
- Clay: `$S/city/sheets/clay_b6/keys_b6_pairs.png` (before = dev7).
- Not yet: MinGW check, in-game, regression (after batch 5 sign-off).

## Update 2026-10-05 ~23:00

- Batch 5 (dev7) in-game shots done (`/tmp/city/dev7_shots`, 23): pools turquoise from the air and lit at night, rejas,
  knee walls, pickets, coral rock, paved yards, CBS gardens back, no palm in the gas canopy. All good. Before shots
  (`/tmp/city/dev7_before_shots`) running, then the regression (`/tmp/city/reg_dev7_log.txt`).
- Batch 6 (dev8): fixed a dangling-else bug (the eyebrow `if` had been inserted inside the conch porch if/else chain,
  so eyebrow houses lost their porch). jobs8 restarted 22:35 (syntax ok 22:43, building nt_dev8.exe). Also raised Keys
  houses' rails painted. Clay: `$S/city/sheets/clay_b7/{keys_pairs,conch_two_storey}.png`. Changelog draft
  `/tmp/city/msg/changelog_b6_keys.txt`.
- Batch 7 started in `$S/dev9` (= dev8 + edits): side-gabled ranches (33%) and block houses (20%) (blockstyle b.roof;
  massing: CBS no longer forces hip), flat-roofed Mediterranean Revival houses (25%, not corner-tower; `medFlat` in
  buildArchHouseBody: flat roof + parapet + tile pent on the front), buildings.h pavedAt comment updated. citylab:
  metric 2.9% -> 2.3% (with batch 6), Westbrook 3.7 -> 2.9%. Clay: `$S/city/clay_med/med.png`.
- Tour stop 5 repeat queued (`/tmp/city/tour5.sh`, after jobs7).

## Update 2026-10-05 ~23:55

- Main is now 6afc8b2 (41f1af1: renderer GPU decor plants; src/render + src/shaders only; src/world = 111efa9's). dev8/dev9
  were patched with `git diff 111efa9 6afc8b2` (non-world files) and verified equal to 6afc8b2 outside src/world.
- Batch 5 (dev7): shots + 7 before shots done; sheets `$S/city/sheets/game_b5/{pools_ingame,fronts_ingame}.png`. Regression
  running (jobs7, started 23:17; 18/27 passed, 0 failed at 23:48). Then: `regcmp.py reg_dev4b_log reg_dev7_log`, placescmp,
  `/tmp/city/snap_b5.sh` (copies dev7 world files into the shared tree, blobs -> /tmp/city/b5_blobs.txt, MinGW check ->
  /tmp/city/syntax_shared_b5.log), then fill `/tmp/city/msg/snapshot_b5_draft.txt` (changelog `/tmp/city/msg/changelog_b5_yards.txt`,
  last bullet fixed: renderer does NOT use pavedAt). Draws: the world is one draw per visible cell (world_render.cpp
  drawGBufferVP), so the yards add triangles, not draws; the per-shot draw samples are streaming noise.
- Batches 6+7 are tested together as "batch 6" in `$S/dev9` (6afc8b2 + batches 5-7): jobs9 (`/tmp/city/jobs9.sh`, status
  `/tmp/city/jobs9.status`): syntax ok 23:09, build nt_dev9.exe queued behind other agents' builds; after tour5: 21 shots
  (`/tmp/city/dev9_shot_args.txt` -> /tmp/city/dev9_shots), regression (reg_dev9_log), 14 before shots on nt_dev7
  (`/tmp/city/dev9_before_shot_args.txt`). Changelog `/tmp/city/msg/changelog_b6_keys.txt` (5 bullets incl. batch 7).
  worldcheck for dev9 and dev10 queued (`/tmp/city/wc9.sh` -> `$S/wc/dev9.txt`, `$S/wc/dev10.txt`).
- Batch 8 in `$S/dev10` (= dev9 + edits): frontage fill (buildings.h OpenLot::street + fillFrontage decl; buildings.cpp call
  after infillBlocks; blockstyle.cpp FrontMix/frontMix/fillFrontage incl. transit-corridor prune; cellgen.cpp
  buildVacantLot + street parking wall/sign + no dumpster on street lots; sitegeo.cpp openLotSign (FOR SALE board / P sign)),
  temple-front conch houses (blockstyle call site adds MK_STEP_FRONT 1.3 for BS_HOUSE conch; massing.cpp conch porch beside
  the bay, piers), Keys motels MK_SPLIT. citylab: `--openlots x,y,r` (needs -DCITYLAB_FRONTAGE), triCounts now prints open
  lot tris. Fill: 4299 gaps -> 1675 vacant, 731 street parking, 649 side yards, 458 service yards. Clay:
  `$S/city/clay_b10/{fill_street,fill_top,top2}.png`, `$S/city/clay_b10k/keys_b8.png`. Changelog draft `/tmp/city/msg/changelog_b8.txt`.

## Update 2026-10-06 ~00:10: batch 5 SENT (snapshot-safe)

- Batch 5 regression 27/27 (0 changed), places identical, counts unchanged. Copied into the shared tree on 6afc8b2
  (`/tmp/city/snap_b5.sh`; blobs `/tmp/city/b5_blobs.txt`: buildmesh f7d6ee6, facadedetail 37e7bad, cellgen a50f39b,
  blockstyle 729fab9; others = HEAD), MinGW 0 errors 0 warnings. Message `/tmp/city/msg/snapshot_b5_fill.txt` sent to main.
- Owed to the lead: tour stop 5 repeat numbers (tour5 started 00:06: m6 then dev4b; logs /tmp/city/tour5_{m6,dev4b}_log.txt;
  compare with /tmp/city/tools/tourcmp.py).
- Next snapshot: batch 6 (= dev9 = batches 6+7) after jobs9; then batch 8 (dev10, `/tmp/city/jobs10.sh` written, NOT started:
  start it when dev10 is final; it waits for jobs9's build, then syntax/build, then after jobs9 the shots/regression;
  needs `/tmp/city/dev10_shot_args.txt` = `$S/city/clay_b8v/shots.txt` from clab10e).

## Update 2026-10-06 ~00:52

- Coordinator: batch 5 = snapshot 32 (tree 16e965d4 = 6afc8b2 + my 4 files), merging if checks pass. USE `git hash-object -w`
  on sign-offs (template `/tmp/city/snap_tmpl.sh`, batch 6 script `/tmp/city/snap_b6.sh` - if HEAD is still 6afc8b2 the
  shared copies equal dev7 (batch 5), not HEAD: relax the check then). Showcase views sent to the lead.
- Tour stop 5 repeat sent to the lead (batch 4 = main: 712/769 vs 714/769 draws).
- jobs9 (batch 6 = dev9): nt_dev9.exe built 00:32; shots started 00:48 -> regression -> before shots (nt_dev7).
- Batch 8 (dev10) now ALSO holds mobile homes (AR_HOUSE_TRAILER = 55, buildTrailer in massing.cpp, trailerDepth /
  carportHalfDepth helpers, carport + back-fence returns follow the home; palettes: towns/keys/farms; no pools, no paved
  yards, no front walk; deck + steps; awnings on half). 149 mobile homes. Changelog `/tmp/city/msg/changelog_b8.txt`.
  jobs10 restarted 00:50 (syntax -> build -> after jobs9: 16 shots `/tmp/city/dev10_shot_args.txt`, regression, before
  shots on nt_dev9). final10.sh reruns citylab stats/perf/views (`$S/city/clay_b11f`) + worldcheck (`$S/wc/dev10.txt`).
- jobs11 (`/tmp/city/jobs11.sh`): after jobs10, the full 16-stop tour on nt_dev10.exe -> /tmp/city/tour_dev10 (review the
  scorecard frames, then the final report).
- Known: Ten Palms log metric 11.3% is the stilt houses (one signature, but roof kinds and decks vary inside it).

## Update 2026-10-06 ~01:32 (after the container restart at 00:57)

- Main = 8fb1e66 (batch 5 merged, PR #32). dev9 = 8fb1e66 + batch 6 exactly; nt_dev9.exe valid.
- jobs9b.sh relaunched batch 6: remaining 16 shots (19 of 21 done by 01:26), regression, before shots -> "jobs9 done".
  Message pre-filled `/tmp/city/msg/snapshot_b6_fill.txt` (placeholders @BASE@ @SYNTAX@ @NCHANGED@ @BLOBS@ @REG@ @PLACES@);
  snapshot script `/tmp/city/snap_b6.sh` (hash-object -w; shared copies must equal HEAD 8fb1e66).
- Batch 8 (dev10) now = frontage fill + mobile homes + conch bays + Keys motel office ends + (folded batch 9 polish)
  L-conch porches in the inner corner (massing.cpp notch, facadedetail notchPorch incl. conch) and painted gallery
  ceilings (haint blue in the Keys on half). nt_dev10_pre9.exe = before the fold. jobs10 restarted 01:30 (syntax, build,
  after jobs9: 19 shots, regression, before shots on nt_dev9). final10b.sh: citylab stats/perf/far + worldcheck after the
  MinGW build. jobs11: full tour after jobs10. dev12 = dev10 (no separate batch 9 now).
- Changelog for batch 8 incl. the polish: `/tmp/city/msg/changelog_b8.txt`.

## Update 2026-10-06 ~02:25

- Batch 6 (dev9) is NOT signed off. Its first story regression (01:31) ended in a plain "Segmentation fault" in mission 10
  (bagman, stage 7, streaming Calle Luna) after 9 passes (`/tmp/city/reg_dev9_log.txt`, `/tmp/city/reg_dev9.out`).
  - nt_dev7 (batch 5, passed 27/27) predates 41f1af1 (the GPU decor plants), so either batch 6 or 41f1af1 can be the cause.
  - The coordinator's triage runs in `/tmp/city/jobs10c.sh` (status `/tmp/city/jobs10.status`): bagman alone 3x on
    nt_dev9 and 3x on `/tmp/city/nt_m8.exe` (a clean main 8fb1e66 build; source `$S/m8`), alternating. Logs:
    `/tmp/city/bag_{dev9,m8}_{1,2,3}_log.txt` and `.out`. If main crashes too: tell the lead, keep batch 6 out of it.
  - Then jobs10c runs batch 8 (dev10): 19 shots, the regression, before shots. jobs11b (tours 2-5, then 13-15) follows.
  - ASan: do it WITHOUT -g, only once memfree is 4500 or more, and one compile at a time (an -O1 -g ASan compile helped
    cause an OOM kill at 02:05). Queued: `/tmp/city/asan12.sh` (worldcheck on dev12, status `/tmp/city/asan12.status`).
  - The batch 6 pair sheets are in `$S/city/sheets/game_b6/pairs_*.png`.
- Batch 8 (dev10): the folded worldcheck passed (`$S/wc/dev10.txt`), citylab stats/perf/far are in `/tmp/city/clab10_*.txt`, and
  `/tmp/city/nt_dev10.exe` is built.
- **Batch 9 (house forms) is in `$S/dev12`** (= dev10 + edits):
  - `blockstyle::houseForm(b)`: bungalow forms (Craftsman side gable with the swept porch roof and a shed or gable dormer,
    hipped with a hipped dormer, airplane with an upper room, Mission (city) with a flat roof, a curved gable and a loggia)
    and the Foursquare (AR_HOUSE_TWO, squarish, towns 55%, city 40%, elsewhere 15%).
  - `formKey(b)` goes into the repetition signature. It also covers the stilt-house roof and deck kinds and the mobile
    home's double-wide and roof-over; buildStiltShack now takes those kinds from blockstyle.
  - `massingPorch(b)` makes facadedetail skip the door canopy. The porches use porchKind: the entry at the door, posts and
    rail clear of the door and of population.cpp's resident slot.
  - Other changes: Craftsman earth tones in facadeFor; ROOF_HIP for the hipped forms and ROOF_FLAT for the Mission
    bungalows; the flat Med houses' parapet now has its outer face (plainWalls).
  - citylab: `-DCITYLAB_FORMS`, `--forms x,y`. Clay views are in `$S/city/clay_b9` (before: `clay_b9_before`); the
    views file is `$S/citylab/views_b9.txt`. The log metric went from 2.3% to 2.2% (Okahatchee 4.4 to 3.7, Harlow 3.8
    to 3.4, Westbrook 2.9 to 2.7) before the stilt and mobile-home keys were added.
  - Changelog draft: `/tmp/city/msg/changelog_b9_houses.txt`.

## Update 2026-10-06 ~05:40

- Batches 6, 8 and 9 and the roads.cpp suffix fix (blob 4569edfb, from the lead) get ONE combined sign-off, tested on
  `/tmp/city/nt_dev13.exe` = main d3c2c3f + 9 world files (`$S/dev13`).
  - MinGW: 0 warnings.
  - ASan + bounds worldcheck: clean (`/tmp/city/wc_dev13_asan2.txt`).
  - In-game shots: 43 of 43 in `/tmp/city/dev13_shots`, all reviewed.
  - Regression: running (`/tmp/city/jobs13b.sh`, status `/tmp/city/jobs10.status`), then the tours.
  - Snapshot script: `/tmp/city/snap_b13.sh`.
- Bagman triage: 0 segfaults in 3 runs on nt_dev9 and 0 in 3 on nt_m8. Every run failed at stage 7 on the test's
  480 s timeout because of the SkyLine train timing when the mission runs alone.
- NEXT batch: drop the storefront glass stopgap. These are the two `lerp(glass, vec3(0.82, 0.88, 0.9), 0.45)` lines in
  blockstyle.cpp (facadeFor and towerLook). The renderer's fix is merged (PR #35, main 9e7ace0); until the stopgap goes, the
  two compound.

## Update 2026-10-06 ~11:10: everything signed off

- Batches 6+8+9 + the roads.cpp suffix fix: merged as PR #37 (main b907098). Tested on nt_dev13: 27/27, ASan clean, 43
  shots, tours 2-5 and 13-15.
- Batch 10 (storefront glass stopgap removed; blockstyle.cpp blob 298c02dc on top of e4efe94): signed off at 11:10.
  Tested on nt_dev14 (b907098 + the file): 27/27, storefront before/after sheets in `$S/city/sheets/game_b10/`.
- Nothing is queued. Candidates for later batches:
  - Grove and Westbrook ranch rows still read alike (tour stop 13): wall colour and form variety;
  - Key Coral villas (5.8%);
  - balcony slabs on the downtown and beach condo towers.

## Update 2026-10-06 ~13:15: batches 11-14 in the pipeline (from the lead's 11:12 list)

- Lead's order (12:1x): sign off batch 11 (overlap) once nt_dev15's shots, regression and tours pass (changelog: Bodega La
  Luna b29652 -> b29653, Grove Pantry 0.97 m, Key Coral Villa 0.16 m; confirm the stick-up encounter at the new bodega);
  then batch 12 (storefronts only on street walls, shader + world, base facade.hlsli on HEAD, the lead merges); then
  batch 13 (rooftops + trash bags: report bag piles in view at tour stops and the tri delta; props have no distance LOD,
  only a cull distance); then item 3 (ranch colours/porches, Key Coral villas).
- Trees (all = main b907098 world files + batch 10 blockstyle, i.e. HEAD's src/world):
  - `$S/dev15` batch 11: buildings.h (garageKind), buildings.cpp (placement + dead-end guard), buildmesh.cpp (garage
    branches), blockstyle.cpp (pavedRects, garage rows in plots). worldcheck: 2,769 -> 0 pairs, passed.
  - `$S/dev17` = dev15 + batch 12: buildmesh.cpp (streetFacing, facadeWalls vertex alpha 0.5), src/shaders/facade.hlsli
    + world.hlsl (shadeFacade(..., streetMark); storefront and signBand gated).
  - `$S/dev18` = dev17 + batch 13: buildmesh.cpp (waterTank, roofPlant, balconyBands, parapet caps + far-LOD parapets
    via Ctx::farRoof, tower crowns 4 every LOD), massing.cpp (roof plant + antennas on archetype roofs, condo balconies,
    far-LOD parapets), propmesh.cpp (trashBag: 6 sacks, ~636 tris/pile vs ~110).
  - `$S/dev19` = dev18 + item 3: blockstyle.cpp (kRanch, ranch/two/split colours, ranchPorch, villaForm, formKey),
    massing.cpp (ranch porch, villaColonnade, villaBalcony, modernPergola), facadedetail.cpp (no tiled portico on Med
    villa forms 1/2), buildmesh.cpp (garage door colours). Not built yet.
  - `$S/dev16` = dev15 + batch 13 (for citylab tri counts only). Item 3 patches saved in `$S/item3/`.
- Jobs (status files in /tmp/city): jobs15 (dev15 shots, hq shots, regression, stick-up encounter, tours) -> jobs17
  (dev17) -> jobs18 (dev18); compile chains cqchain (wc_sf, nt_dev17, citylab dev16/dev15 perf) -> cqchain2 (nt_dev18,
  citylab dev18 clay views views_b13). Shot lists: /tmp/city/dev15_shot_args.txt, dev17_shot_args.txt, dev18_shot_args.txt.
- worldcheck ($S/wc/worldcheck.cpp) has the footprint pass + FP_NEAR / FP_ALLBOX / FP_SHOTS / FP_SFSHOTS / FP_BAGS.
- Bag piles: 4,568 on the map; within 60 m: stop 2 0, stop 3 0, stop 4 1, stop 5 1 (3 within 90 m), NPS showcase 6.
- Drafts: /tmp/city/msg/changelog_b11.txt, changelog_b12.txt, aviation_lights.txt.

## Update 2026-10-06 ~13:30: WIND-DOWN (the lead: finish batch 11 and items 2+3 to sign-offs; park the rest)

- PARKED (not signed off, not in the shared tree):
  - Batch 12, storefront windows only on street walls: `$S/dev17` (= dev15 + buildmesh.cpp streetFacing/facadeWalls
    vertex alpha 0.5 + src/shaders/facade.hlsli and world.hlsl: shadeFacade(..., streetMark) gating storefront and
    signBand). Syntax-checked (MinGW 0 warnings), never built or shot. Shot list ready: /tmp/city/dev17_shot_args.txt
    (side/back walls by day and night, street fronts unchanged), changelog draft /tmp/city/msg/changelog_b12.txt.
  - Soft trash bags: `trashBag()` and the PROP_TRASH_BAGS case in `$S/dev16/src/world/propmesh.cpp` (6 tied sacks,
    ~636 tris per pile vs ~110; props have only a cull distance, no far mesh). Bag piles within 60 m: stop 2 0, stop 3
    0, stop 4 1, stop 5 1, the North Porto Sol showcase 6; 4,568 on the map.
- In progress to sign-off: batch 11 on nt_dev15 (jobs15), then batch 13 = items 2+3 in `$S/dev20` (= dev15 + rooftops
  + ranch/villa variety, no storefronts, no bags): chain /tmp/city/cq20.sh, tests /tmp/city/jobs20.sh.

## Update 2026-10-06 ~14:55 (after the 13:50 worker restart)

- HEAD is now a2cd96b (PR #41). Its src/world equals b907098 + batch 10, so dev15 = HEAD's world + batch 11 (4 files) and
  dev20 = dev15 + batch 13 (buildmesh.cpp, massing.cpp, blockstyle.cpp, facadedetail.cpp); dev20's buildings.h/.cpp =
  dev15's. Shared tree src/world = HEAD (nothing of mine in it yet).
- Batch 11 (dev15): shots after+before reviewed (sheets `$S/city/sheets/b11/pairs1.png`, `pairs2.png`): every garage
  back on its own lot. jobs15 still running (HQ after/before, regression, stick-up encounter, tours 2-5). Changelog
  draft /tmp/city/msg/changelog_b11.txt (@ENC@ = the stick-up result). Blobs to write at sign-off (git hash-object -w):
  buildings.h fda7b1f3, buildings.cpp 4ef3fdb1, buildmesh.cpp 70616389, blockstyle.cpp bfb494a1.
- Batch 13 (dev20, final = "dev20d"): far-LOD budget settled. Far copings dropped (sub-pixel past 420 m, 20k tris
  downtown); far LOD keeps roof plant masses (first HVAC unit only), 6-sided tanks, balcony bands, crown 4. Triangles
  vs dev15 (citylab, /tmp/city/clab_dev15_perf.txt vs clab_dev20d_perf.txt): near +0.3..11%, far +28..30% downtown/
  midtown, +52% beach, +16..21% Calle Luna/North City/Grove. Breakdown measured with toggles ($S/dev20m, a throwaway
  measurement copy; clab_dev20m_*.txt). worldcheck dev20d passed, 0 overlaps, colliders 659,605 (dev15 650,625).
  Changelog draft /tmp/city/msg/changelog_b13.txt (@COST@, @TESTS@ to fill).
- Chains: /tmp/city/cq20d.sh (nt_dev20.exe -> ASan wc_dev20) -> clab14.sh (citylab dev14 tri counts for batch 11) ->
  asan15.sh (ASan worldcheck on dev15). jobs20.sh waits for jobs15 + nt_dev20.exe. Status files: cq20.status,
  clab14.status, asan15.status, jobs15.status, jobs20.status.

## STOP 2026-10-06 ~16:35 (the lead: usage limit, stop now, park everything) - NOTHING RUNNING
- All my chains, games and waiters were killed by PID (jobs15 was on tour stop 3 of 2-5; jobs20 never started).
  Shared tree: nothing of mine (src/world and src/shaders = HEAD a2cd96b). The stray `--help` in the repo root (a
  900x900 character-render PPM from 03:40) is not mine.
- PARKED batch 11, garages kept on their lots: tree `$S/dev15` (HEAD's src/world + 4 files). Blobs written with -w:
  buildings.h fda7b1f3b156af2ca92f480fca4baba5b0375e69, buildings.cpp 4ef3fdb15650249491094be705ad0031fa00828d,
  buildmesh.cpp 70616389162c453b66c16804b22b30ad5f74baae, blockstyle.cpp bfb494a14f3a55257ab0d8f75967cec07dd5da35.
  DONE on nt_dev15: worldcheck 2,769 -> 0 overlaps (passed), ASan clean, 12 shot pairs + the NPS HQ pair reviewed
  (sheets `$S/city/sheets/b11/`), story regression 27/27 (0 changed vs nt_dev14), stick-up encounter 2/2 at the new
  Bodega La Luna (b29653), triangles near 0..-4.6%, far 0..-0.9%. STILL TO RUN: tours 2-5 (only stops 2 done), then
  the shared-tree MinGW syntax check at copy-in. Sign-off text ready: /tmp/city/msg/signoff_b11.txt (@TOUR@,
  @SHAREDSYN@ to fill).
- PARKED batch 13 = items 2+3 (roof plant/tanks/copings/balconies/crowns; ranch colours and porches; villa forms): tree
  `$S/dev20` (= dev15 + 4 files; buildings.h/.cpp are batch 11's). Blobs written with -w: buildmesh.cpp
  f6747fbdf47249cb4f6bc3384540ec5aaa55d32a, blockstyle.cpp 7da1951e4af7ec1fc65ff2fa42005b81737772bd, massing.cpp
  ccf1712cf46b9ab392e071242a7ded1affec56c4, facadedetail.cpp 1c903d097a67ccd81abc265c02a9395bae768e6d. DONE:
  worldcheck passed (0 overlaps), ASan clean, MinGW 0 warnings, nt_dev20.exe built, citylab triangles (near +0..11%,
  far +28..30% downtown, +52% beach) and clay views. STILL TO RUN: all in-game tests = /tmp/city/jobs20.sh (16 shot
  pairs, 3 HQ pairs, regression, tours 2-5 and 13); it first waits for the line "jobs15 done" in jobs15.status, so
  append that line (or drop the wait) before `cd /tmp/city && nohup setsid ./jobs20.sh &`. Sign-off draft:
  /tmp/city/msg/signoff_b13.txt; aviation-light needs: /tmp/city/msg/aviation_lights.txt; sheets: /tmp/city/sheets_b13.sh.
- PARKED, never started as batches: storefront back-wall gate (`$S/dev17`: buildmesh.cpp + src/shaders/facade.hlsli and
  world.hlsl; never built; /tmp/city/msg/changelog_b12.txt, dev17_shot_args.txt); soft trash bags
  (`$S/dev16/src/world/propmesh.cpp` trashBag(): ~636 vs ~110 tris per pile; 0-1 piles within 60 m of tour stops 2-5).
- Resume: exes /tmp/city/nt_dev14.exe (base), nt_dev15.exe (batch 11), nt_dev20.exe (batch 13); games only via
  tools/run.sh with WINEPREFIX=/tmp/wine_city (run.sh takes a slot), builds via build.sh (OUT=...); one game and one
  build of mine at a time; memfree >= 2000 before any compile, >= 4500 for ASan (no -g).

## Update 2026-10-12 ~15:35 (resumed; main 894e7fb, its src/world = a2cd96b's)
- Disk: deleted my old exes, shot/regression BMP folders, clay PPMs, wc/citylab binaries and old trees (dev3..dev14,
  dev18, dev19, base4, b3, m4..m10). Kept: trees dev15 (b11), dev16 (bags src), dev17 (storefront src), dev20 (b13),
  dev22 (new: b11+b13+storefront+bags), item3; exes /tmp/city/nt_dev15, nt_dev20, nt_dev22; sheets in $S/city/sheets.
- Batch 11 SIGNED OFF 15:3x (tours 2-5 4/4; the 4 files are in the shared tree; shared MinGW check 0/0). Text:
  /tmp/city/msg/signoff_b11.txt.
- Batch 13: far ring justified (msg/signoff_b13.txt FRAME COST: whole map 1.097 M -> 1.209 M far tris, worst case
  Westbrook unchanged; far HVAC trim measured 7.6k at stop 2, offered not applied). In-game tests: /tmp/city/jobs22.sh
  (status jobs22.status; shots with --debugtext give in-view tris; HQ; regression; tours 2-5, 13 on dev20 and 13 on
  dev15).
- Batches 12 + 14 (storefront gate + soft bags) together in $S/dev22: worldcheck passed, ASan clean, MinGW 0 warnings,
  /tmp/city/nt_dev22.exe built. Tests: /tmp/city/jobs23.sh (after jobs22): 19 shots (dev22_shot_args.txt: storefront
  side/back walls day/night + 3 bag views), regression, tours 2-5. Bag piles near NPS listed in $S/wc/dev22.txt.
- Every game goes through a memory gate (memfree >= 3300) before run.sh: run.sh launches anyway after 10 min.

## Update 2026-10-12 ~19:30
- main 61a3b7b (MSVC fixes); batch 11 = PR #45 (snapshot 43, 590854d). All code must be MSVC-safe: scan with
  `clang++ -std=c++17 -fsyntax-only -Wvla -Wgnu -Wpedantic -I<tree> $S/wc/worldcheck.cpp` (0 diagnostics so far).
- Batch 11b SIGNED OFF 19:3x (interiors.cpp garageIntrudes via garageKind/garageSide/carportHalfDepth + roof; blob
  423f6f25; in the shared tree). Hosts identical; old check flagged 48, new 0. Text: /tmp/city/msg/signoff_b11b.txt.
- dev15b (b11 + 11b) tree kept; dev20 and dev22 trees now carry 11b's interiors.cpp. cq20e.sh builds nt_dev20b.exe and
  nt_dev22b.exe (with 11b). jobs22c.sh: HQ + 9 item-3 street views (nt_dev20 vs nt_dev15), then regression and tours on
  nt_dev20b -> "jobs22 done". jobs23.sh (batch 12+14) then runs on nt_dev22b vs nt_dev20b.
- Batch 13 in-view tris (debug overlay, 16 views): +0.2..3.1%. Sheets: $S/city/sheets/b13/{roofs1,roofs2,keycoral,ranch,
  overlays}.png. Item-3 street views list: /tmp/city/dev20_item3_args.txt (from citylab --villaforms).

## Update 2026-10-12 ~22:40
- Aviation beacons (the lead's queued item, "batch 15"): in $S/dev22 with the storefront gate and the bags. landmarks.cpp:
  Solaris One spire tip gets its type-4 light, 4 tower-top corner beacons (lamp EA_BLINK phase 90 + light); City Hall
  tower lamp gets its type-4 light. leisure.cpp: Ferris wheel rim-top beacon (new), drop tower lamp gets its light.
  Verified: sugar mill stacks (70 m) already have lamps (EA_SLOWBLINK) + type-4 lights; radio mast, STS cranes, control
  tower have them; stadium is 50 m with no separate masts; no bridge towers exist; campus bell tower 48 m, hospital
  towers ~47 m, lighthouse 44 m, hotels low. cq22c.sh builds nt_dev22c.exe; jobs23 (22 shots incl. 3 night beacon views)
  runs after jobs22c/jobs22d.
- Batch 13: HQ after done; HQ before was OOM-killed at 21:23 (jobs22d.sh retries it after jobs22c). Item-3 street views
  done (sheets item3_villas.png, item3_ranch.png). Regression on nt_dev20b started 22:34.

## Update 2026-10-13 ~04:30
- Batch 13 SIGNED OFF ~03:50 and accepted for snapshot 47 (keep the far HVAC and the bands). 4 files in the shared tree.
- Batches 12 + 14 + 15 (storefront gate, soft bags, aviation beacons) now on CURRENT HEAD 67e6159 (night B changed
  facade.hlsli, so the shader edit is re-applied onto HEAD's: 3 lines + world.hlsl's call): trees $S/dev23 (after: HEAD +
  batch 13 + buildmesh/propmesh/landmarks/leisure + facade.hlsli/world.hlsl) and $S/dev23b (before: HEAD + batch 13).
  Exes /tmp/city/nt_dev23.exe, nt_dev23b.exe (cq23.sh). dev23 worldcheck passed, clang scan 0, MinGW 0/0.
- jobs23.sh runs: 22 shots after/before (dev22_shot_args.txt: 16 storefront side/back day+night, 3 bag views, 3 night
  beacon views), regression (quality 0, retry on OOM), tours 2-5 on nt_dev23. Sign-off draft /tmp/city/msg/signoff_b12_14.txt
  (needs the beacon section, blobs on HEAD+batch13).
- OOM killer: the machine runs 4-5 games; my ultra HQ and default-quality regressions got killed. Use --quality 0 for
  regressions, gate 3500-4500 for big runs.

## Update 2026-10-13 ~05:40
- HEAD is now ab5995c (snapshot 47 = 67e6159 + batch 13 + small game/anim edits). The shared tree's 6 target files
  (buildmesh f6747fb, propmesh e2e3022, landmarks 38e1433, leisure 0279fc3, facade.hlsli d4fae17, world.hlsl 4f3089a)
  equal HEAD, so $S/dev23's 6 files go straight on top.
- After shots (nt_dev23) done 05:31: /tmp/city/dev22_shots (19 kept; the 3 old beacon views were useless: the lamps blink
  on 18% of the time (pattern 1) and the pier pose looked into a building, so they were deleted).
- The lead's hold: no new game before 05:45 or /tmp/city/go. jobs25.sh was stopped (while holding) and replaced by
  /tmp/city/jobs26.sh (pid 21729): before shots (dev22_shot_args_b.txt, 19 views, nt_dev23b), then beacon close views
  (dev22_beacon_args.txt: beacon_solaris_top, beacon_solaris_mid, beacon_wheel_top) on nt_dev23b and nt_dev23 with
  settings.ini "reduce_flashing = 1" (written before, removed after each run; trap on exit), then the regression
  (quality 0, 2 tries), tours 2-5. Status: /tmp/city/jobs23.status ("jobs23 done" at the end).
- Then: sheets ($S/city/b12_sheets.sh -> $S/city/sheets/b12/, add the beacon pairs from dev22_beacon_before/after),
  prop cost ($S/city/propstats.py before-log after-log), fill /tmp/city/msg/signoff_b12_14.txt, copy the 6 files from
  $S/dev23 into the shared tree, git hash-object -w, MinGW in the shared tree, SendMessage "city snapshot-safe: ...".

## Update 2026-10-13 ~06:45 (the lead: wind-down; finish this batch with tests + regression, sign off, then park)
- Before shots done (19, rc 0). Storefront pairs reviewed: OK. Side/back walls lose the shop (day and night), street
  fronts unchanged, and in the "front" views the neighbours' narrow side walls lose stray shop windows too.
- Bags found two flaws in game: (1) all sacks render black: MAT_PLASTIC is the vehicles' fixed black trim (albedo 0.03
  whatever the tint), so the green and white sacks never showed; (2) the lying sack reached 1.0 m (x scale) from the
  pile centre vs the old box pile's ~0.8, so it could touch a parking meter (seen in bags_nps_close_day).
  Fix (bag pile v2, $S/dev23 propmesh.cpp, v1 kept as /tmp/city/propmesh_dev23_v1.cpp): sacks use `metal`
  (MAT_METAL_PAINTED, tintable, albedo 0.6) with colours scaled for it (black 0.055, green (0.08,0.18,0.11), white 1.0),
  lying sack {-0.4,-0.3, yaw 2.3, h 0.44} -> every sack within 0.75 m (/tmp/city/pile_extent.py).
- cq23c.sh builds /tmp/city/nt_dev23c.exe (clang scan, MinGW, exe; status /tmp/city/cq23c.status).
- jobs26 stopped (and its run.sh, before any game started); /tmp/city/jobs27.sh (pid 8637) runs: beacon views before
  (nt_dev23b) -> waits for nt_dev23c -> beacon views after (nt_dev23c) -> bag views again (dev22c_bag_args.txt ->
  /tmp/city/dev22c_bag_shots) -> regression (reg_dev22c_log.txt) -> tours (tour_dev22c_log.txt) -> "jobs23 done".
- Sheets so far: $S/city/sheets/b12/storefront1-3.png, bags.png (v1). Props: $S/city/propstats.py.
- ~08:10: nt_dev23c built (clang scan 0, MinGW 0/0). Beacon views before/after done (sheet $S/city/sheets/b12/beacons.png):
  Solaris top from 90 m shows the 2 near corner beacons, the wheel its rim-top beacon; at 350 m the corners are ~1 px.
  /tmp/city/jobs28.sh (pid 27859) waits for jobs27 to end, then runs the BEFORE tours on nt_dev23b
  (tour_dev22b_log.txt), so the tours compare on the same base ("jobs28 done" in jobs23.status).
- ~08:30: bag v2 shots done (/tmp/city/dev22c_bag_shots; sheet bags.png = before vs v2): white and green sacks show, the
  lying sack sits beside the meter. Regression on nt_dev23c started 08:23 (reg_dev22c_log.txt). /tmp/city/asan23.sh
  (pid 30678) waits for memfree >= 4500, then ASan worldcheck on $S/dev23 (/tmp/city/wc_dev23_asan.txt; status
  /tmp/city/asan23.status). Sign-off draft is complete except @SHAREDSYN@ @ASAN@ @REG@ @TOURS@
  (/tmp/city/msg/signoff_b12_14.txt). propmesh blob is now 6eb6739 (v2).
- ~09:20: regression on nt_dev23c 27/27 passed (buildings, restyles, story places, 29 interior hosts identical to
  reg_dev20); ASan worldcheck on dev23: 0 reports, passed. Tours on nt_dev23c running (09:15), then jobs28's before
  tours on nt_dev23b. Compare with `python3 $S/city/tools/tourcmp.py /tmp/city/tour_dev22b_log.txt /tmp/city/tour_dev22c_log.txt`.
  Left in the draft: @SHAREDSYN@ (MinGW in the shared tree after copying the 6 files) and @TOURS@.
- ~10:45: tours done (4/4 stops each, no segfault). Draws mean/max nt_dev23b -> nt_dev23c: stop 2 684/755 -> 681/752,
  stop 3 567/577 -> 567/581, stop 4 383/450 -> 373/443, stop 5 583/597 -> 701/723 (!). Only my 6 files differ between
  the trees; the simulations diverged (fender bender + hug only in the before run). /tmp/city/jobs29.sh (pid 10299)
  re-runs stop 5 alone: after, before, after, before (tour5_dev23c_N_log.txt / tour5_dev23b_N_log.txt; status lines
  "tour5 ..." and "jobs29 done"). Then /tmp/city/snap_b12.sh copies the 6 files into the shared tree + MinGW there.

## PARKED 2026-10-13 ~12:00 (the lead: wind-down for usage; batch accepted) - NOTHING RUNNING
- Batches 12 + 14 + 15 (storefront back-wall gate, soft trash bags, aviation beacons) SIGNED OFF ~11:45 and ACCEPTED:
  they go into snapshot 50 (tree 1212d0c7) on PR #47 (open until the animation agent's Codex fix lands). The 6 files
  are in the shared tree on top of HEAD 7166a69 (blobs in /tmp/city/b12_blobs.txt): buildmesh.cpp 46cfe87,
  propmesh.cpp 6eb6739, landmarks.cpp cb2b991, leisure.cpp 8a227dd, facade.hlsli 07a30a1, world.hlsl 32126ee.
  Sign-off text: /tmp/city/msg/signoff_b12_14.txt. Sheets: $S/city/sheets/b12/ (storefront1-3, bags, beacons, tours).
- Test results: regression 27/27 (nt_dev23c), ASan worldcheck 0 reports, MinGW 0/0 private + shared, clang scan 0;
  tours equal (stop 5 standalone x2 each: 710-712/767 both builds).
- Nothing of mine is running: no chains, games, builds or waiters. Exes kept: /tmp/city/nt_dev23c.exe (the signed-off
  build), nt_dev23b.exe (its before); nt_dev23.exe deleted; shot BMPs converted to PNG (/tmp/city 200 -> 130 MB).
- Private trees: $S/dev23 = the signed-off state (HEAD 67e6159 + batch 13 + the 6 files; world/shaders equal the
  shared tree now), $S/dev23b = its before. Older $S/dev*, base*, b3 trees are obsolete (all merged or superseded).

### Ideas for next (none started)
- Props that use MAT_PLASTIC with a colour render black (it is the vehicles' fixed black trim, albedo 0.03 whatever the
  tint): PROP_CONE (orange + white bands), PROP_BARRIER (orange / white), PROP_DUMPSTER parts (propmesh.cpp lines ~639,
  737, 759). Check in game; the fix is `metal` (MAT_METAL_PAINTED, tint x 0.6) as for the bags. Needs the lead's OK
  (propmesh.cpp was cleared for the bags only).
- Aviation lights: the type-4 LightInstance blinks on the CPU (world_render.cpp case 4: 0.25 s of 1.6 s, phase by
  pos.x) while the lamp's emissive blinks in world.hlsl pattern 1 (0.23 s of 1.25 s): the two are out of step. Syncing
  them is the renderer's (world_render.cpp) - propose to the lead / render agent.
- Solaris corner beacons are 0.6 m in the near LOD: about a pixel at 350 m. Could scale the near-LOD lamp with
  distance or use 0.9 m like the spire lamps if the lead wants them to read from the street.
- Bag piles share one mesh (rotated); a second variant (3-4 sacks, or all black) via PropInstance.variant would add variety.
- Frame-cost method note: a 4-stop tour's stop 5 depends on the run history (583 draws in 4-stop runs vs ~711 when
  started at stop 5); compare stops run the same way, or standalone.
- Capturing blinking emissives: write "reduce_flashing = 1" into the prefix's settings.ini for the run (and delete it
  after); the beacon views are in /tmp/city/dev22_beacon_args.txt.

## RESUMED 2026-10-19 ~13:35 (HEAD 0d67933 = snapshots 49-51 on PR #47, incl. batches 12/14/15; main af8a066 = v0.02.6)
The lead's list: (1) FIRST the MAT_PLASTIC check (cones, barriers, dumpster parts) + fix + sign-off with a shot pair;
(2) a second bag-pile variant; (3) larger Solaris corner beacons if they should read from the street; (4) other ideas.
Beacon blink sync: the lead relayed my proposal to the night agent. Split: me = worldtypes.h (u8 phase, default 0) +
phases in sitegeo light() type-4 calls and buildmesh antenna beacons; night = world_render.cpp case 4 (+ clock). WAIT for
the night agent's agreement; both halves are signed off together, one snapshot.
- APPROVED: src/render/props_render.cpp one line: add `case World::PROP_TRASH_BAGS:` to the nv = 2 case (PLANTER,
  BARRIER, SIGNAL_SPAN). Guard on HEAD's blob b6e68ab when copying; quote the line in the sign-off. Nothing else there.
- Batch 16 (plastic props): dumpster lids are meant black (0.06) - fine. Cones and barriers use MAT_PLASTIC with
  orange/white -> black. Fix in $S/dev25 (= HEAD + propmesh.cpp: `metal`, orange (1,0.24,0.02) cones, (1,0.28,0.03)
  barriers, white 1.0). Also black by MAT_PLASTIC but NOT my files: cemetery.cpp:806, outskirts.cpp:316,
  transitmesh.cpp:163-168, transitmesh_tram.cpp:91 (sitegeo's M() = makeMat; interiorkit's M() maps plastic to painted).
- Batch 17 in $S/dev26 (= HEAD + dev25's propmesh + bag variant 1 (4 sacks, max 0.71 m) in propmesh.cpp, the per-pile hash
  in cellgen.cpp after buildSiteCell, the props_render.cpp line). Solaris corner size: decide from the street shots.
- cq25.sh builds nt_dev24.exe (HEAD, before) and nt_dev25.exe; jobs30.sh: before day views (dev25_day_args.txt: road
  works + dumpsters near North Porto Sol, bags) + before night Solaris street views (dev25_night_args.txt, Reduce
  Flashing) on nt_dev24, then after day views on nt_dev25. Status: /tmp/city/cq25.status, /tmp/city/jobs30.status.
- worldcheck tool: new FP_WORKS=x,y lists road-work zones and dumpsters near a point ($S/wc/dev24.txt).
- ~13:52 Beacon sync AGREED (night agent: same clock; it does world_render.cpp case 4 + a reduceFlashing flag). My half in
  $S/dev26: worldtypes.h `u8 phase = 0;` after type (blob 63a9e6e, written -w; fixed copy $S/beaconsync/worldtypes.h,
  note $S/beaconsync.txt), sitegeo beaconLight(g, p, color, radius, phase), all type-4 light() calls converted;
  ONE PHASE PER STRUCTURE (the lead: flash together): Solaris 90, radio mast 40, sugar mill 60, crane seed; pattern-7
  lamps (mast, mill) -> pattern 1. Both halves are signed off together, one snapshot.
- Solaris corner lamps 0.6 -> 0.9 m (as the spire's) in dev26. cq26.sh (after cq25): worldcheck -DWC_BEACONS with
  FP_BEACONS=1 (lamp/light phase check) + bag variants, clang, MinGW, nt_dev26.exe, ASan worldcheck.
- ~15:10: before shots done (dev25_before_day: works/dumpsters/bags; dev25_before_night: Solaris views). CONFIRMED in
  game: cones and barriers render black on HEAD; dumpster lids black by design. Street views: station pose was inside
  the structure, plaza/downtown views blocked; from the east (3900,-750,20) only the 1.1 m tip lamp shows (1 px at
  ~720 m, near LOD) -> corners now 1.1 m in dev26 (cq26 killed while waiting; cq27.sh rebuilds nt_dev26 + ASan wc).
  dev26 worldcheck (0.9 m build): passed; aviation lights 154: 97 match their lamp's phase, 0 mismatch, 57 without a
  lamp (buildmesh tower-crown mast/spire lights, z 75-300 m: an idea for later: give them a lamp); bags 2283/4568 variant 1.
- jobs31.sh (pid 22464) after jobs30: far Solaris views before (sol_port_gate, sol_port_south -> dev25_before_night),
  then on nt_dev26: night after (dev26_after_night), day after (dev26_after_day), regression (reg_dev26_log.txt), tours
  dev26 then dev24 (tour_dev26_log.txt, tour_dev24_log.txt). Status lines in /tmp/city/jobs30.status.
- ~15:50: BATCH 16 SIGNED OFF (cones + barriers on `metal`): propmesh.cpp e521a68 in the shared tree on HEAD b71d339
  (= 0d67933 + an anim stairs fix; my files unchanged). MinGW shared 0/0. Sheets $S/city/sheets/b16/. Regression and
  tours to be reported with batch 17 (nt_dev26 carries it). Reported the other MAT_PLASTIC uses (cemetery, outskirts
  backboards, transit third-rail cover, tram insulators) to the lead - not my files.
- nt_dev26 rebuilt with 1.1 m corners (15:30). asan26.sh (pid 32370) runs the ASan worldcheck on dev26 (status in
  /tmp/city/cq26.status). jobs31 waits at its memory gate (the night agent had memory priority until its gate passed).
- ~16:05: batch 16 APPROVED (snapshot 53). NEW (the lead): batch 19 = the other four MAT_PLASTIC sites, mine now
  (cemetery.cpp:806 water barrel, outskirts.cpp:316 prison-yard backboards, transitmesh.cpp:163-168 SkyLine third-rail
  cover + insulator posts, transitmesh_tram.cpp:91 streetcar pole insulators): M(MAT_METAL_PAINTED) at each call site,
  colours /0.6 (hue kept where clamped); NOT sitegeo's M() globally. In $S/dev27 (= dev26 + these 4). Check each in game
  before (nt_dev24) / after (nt_dev27). cq28.sh (pid 860): worldcheck spot query for the poses ($S/wc/dev26_spots.txt),
  then the ASan worldcheck on dev26 (moved from asan26). Then build nt_dev27 (not queued yet).
- ~16:10: spot query done ($S/wc/dev26_spots.txt): cemetery ground 3.47, prison yard 6.90, SkyLine straight along x at
  y -628.6 west of station Solaris (top of rail 15.0), streetcar pole (5232.7,1140.65,1.7) by 42nd St; (3900,-750) and
  (4300,-1100) are over water. Batch 19 views: /tmp/city/dev27_day_args.txt (cem_barrel, prison_backboard,
  skyline_third_rail, tram_insulator). jobs31 stopped before any game; cq28 (ASan on dev26) running; cq29.sh (pid 6059)
  builds nt_dev27 after it; jobs32.sh (pid 6076): before (nt_dev24: far Solaris night RF + batch 19 day), after nt_dev26
  (night RF + bags day), after nt_dev27 (batch 19 day), regression + tours on nt_dev27, tours on nt_dev24.
- ~16:20: the night agent signed off its half (world_render.cpp 2de3ba98, renderer.cpp 0d38d93f); the lead put them AND my
  worldtypes.h 63a9e6e3 (guard 21d26d2) into the shared tree. My batch 17 sign-off lands with night's two files as one
  snapshot. In the snap script: worldtypes.h must equal 63a9e6e3 already (not HEAD's) - list it as "63a9e6e3 on 21d26d2,
  already in the shared tree".
- ~17:15: ASan worldcheck on dev26 (corners 1.1 m): 0 reports, passed; aviation lights 154: 97 match their lamp's phase,
  0 mismatch, 57 without a lamp (buildmesh crown lights); bags 2283/4568 variant 1. nt_dev27 built (clang 0, MinGW 0/0).
  Batch 17 sheets: $S/city/sheets/b17/beacons.png, beacons_zoom.png, bags.png. Result: corners read as a red pixel each
  from ~1 km (far LOD; sol_port_south) where before only the spire showed; inside the near ring (~720 m from the port
  basin, sol_street_east) still under a pixel - a renderer point-sprite for beacons would fix all distances (idea, night
  agent's area). sol_port_gate pose was under the gate canopy (useless). Variant 1 piles read smaller/different.
- Batch 19 before shots fine (barrel, backboard, third-rail covers, tram insulator all black on HEAD).
- ~17:20 memory priority (the lead): after my nt_dev27 game, nothing new until the AI agent's nightwalk game runs AND
  faces' gate (pid 17353) is gone, or 18:00 UTC. jobs32 stopped mid-game; /tmp/city/jobs33.sh (pid 24257) waits for the
  game (timeout pid 21633), saves its log, holds (pgrep -f "^[^ ]*\.exe --autotest.*--autoplay nightwalk"), then the
  regression (reg_dev27_log.txt) and tours on nt_dev27 and nt_dev24. Snap scripts ready: /tmp/city/snap_b17.sh (11 files
  from dev26, guards: propmesh e521a68, worldtypes 63a9e6e3, the rest HEAD) and /tmp/city/snap_b19.sh (4 files from dev27).
- ~17:30: batch 19 after shots done; sheets $S/city/sheets/b19/plastic2.png + plastic2_crops.png: barrel blue, backboard
  white, third-rail cover yellow, tram insulator brown (all black before). jobs33 holding for the AI/faces agents (fallback
  18:00), then regression + tours on nt_dev27 and tours on nt_dev24. Then: snap_b17.sh + sign-off 17+18, snap_b19.sh +
  sign-off 19 (both cite the nt_dev27 regression/tours).
- ~19:12: regression on nt_dev27 (batches 16-19): 27/27 passed, buildings/restyles/story places/29 interior hosts
  identical to reg_dev22c. Tours running (nt_dev27, then nt_dev24); compare with tourcmp.py tour_dev24_log.txt
  tour_dev27_log.txt (if stop 5 drifts, re-run it standalone as before).

## DIRECTION CHANGE 2026-10-19 ~19:20 (the user, via the lead): the procedural world will be replaced by a HAND-BUILT world
(same ~16x16 km map; keep only terrain: ground, coast, water; everything else hand-authored; every building enterable,
every floor, every room). "Finish, then switch":
1. Finish batch 17 = my beacon-sync world side, sign off as planned. 2. DON'T land the bag variant, the Solaris corner
size or the four MAT_PLASTIC sites (procedural work going away) - they're done/tested in dev26/dev27 but HELD.
3. Next job: architecture proposal docs/HANDBUILT_WORLD.md (private tree; send as a sign-off file + summary). Prototype
only to check assumptions. Fold in the AI agent's road-graph/nav requirements (coming after its set 18).
- Beacon-sync-only tree: $S/dev28 = HEAD b71d339 + worldtypes.h, sitegeo, landmarks (phases only, corners stay 0.6 m),
  leisure, airport, port, rural, buildmesh (from dev26). cq30.sh: worldcheck FP_BEACONS, clang, MinGW. Tours (nt_dev27 /
  nt_dev24) still running in jobs33 (superset build: covers the subset).
- ~20:05 proposal work: private tree $S/hand0 (= HEAD b71d339 + docs/HANDBUILT_WORLD.md). Prototypes queued:
  /tmp/city/tb.sh (terrain bake: $S/tb/terrainbake.cpp against $S/proto_tb, which adds `gTerrainBakeNatural` to skip
  the site layout; results $S/tb/run.log, sizes.txt), /tmp/city/stats.sh (worldcheck FP_STATS: floors/areas by style and
  region -> $S/wc/stats.txt). The lead's 2nd update: keep ~35,617 buildings (all enterable), library + per-building
  config/overrides, data size (embed vs pack + autoupdater), runtime budgets (memory, radius, load time, 50-floor tower,
  per-floor collision/nav, NPCs inside), validator at 35k, realistic numbers (library size, throughput, effort).
- ~20:10: DRAFT written: $S/hand0/docs/HANDBUILT_WORLD.md (sections 0-14). Placeholders to fill from the prototypes:
  {{TB_*}} (terrain bake: $S/tb/run.log, sizes.txt) and {{ST_*}} (census: $S/wc/stats.txt; Calle Luna count for the
  pilot). Section 12 waits for the AI agent's road-graph/nav requirements (after its set 18). Then send the doc as a
  sign-off file (blob of docs/HANDBUILT_WORLD.md, new file) with a summary message to the lead.
- Beacon-sync sign-off still pending: cq30 (dev28 clang 0 so far; MinGW waiting for memory) + tours (jobs33). Then
  snap: copy dev28's 7 files (worldtypes.h is already 63a9e6e3 in the shared tree) - write a snap script for dev28.
- ~21:20: census filled into the doc (section 13.2, pilot 439 buildings / 834 floors in 400 m of (1560,350)). Doc read end
  to end; fixes: unit/window ownership (layout owns exterior walls + windows; validator checks every allowed unit),
  strip-mall plans in the effort table (library ~1,850 h; total still ~6,700). Remaining: {{TB_*}} from tb3
  (/tmp/city/tb3.sh, waits for memory; output $S/tb/run.log), section 12 (AI agent: set 17 signed 19:29, now on set 18).
  Then: dev24 tour -> tourcmp -> snap_b18.sh -> beacon-sync sign-off; doc blob (git hash-object -w) -> proposal sign-off.
- ~21:35: tb3 done ($S/tb/run.log): land 183.5 km2 / water 235.9 (land spans 15.8 x 19.5 km); sites change heights on
  3.73 km2 (coast 0.017 to water, 0.016 to land); roads 2.12 km2 more (+91.8/-17.5 m), 0.041 km2 water->land (flattenAlong
  fills under roads < 1 m above water). Filled into the doc (no {{ }} left). Queued (background, one build at a time):
  /tmp/city/ct.sh (worldcheck FP_CELLTRIS on dev28 -> $S/wc/celltris.txt: near/far tris and ms per cell, buildings per
  cell by region; for sections 7.2/8), then /tmp/city/tb4.sh (terrainbake region breakdown + largest raises with the
  nearest road -> $S/tb/run4.log). HEAD is now 1a83e24; my 7 beacon files are unchanged b71d339->HEAD (snap guard OK).
  Draft sign-off: /tmp/city/msg/b17_draft.txt ({{SYN}}, {{FILES}}, {{TOURS}} to fill).
- ~21:45: ct done ($S/wc/celltris.txt): near cells mean 39k tris (p99 144k, max 171k), far 2.1k; near gen 7 ms mean,
  85 max; buildings/cell max 84 (Calle Luna), Westbrook mean 42 max 62 -> doc 7.2/8 corrected (near shell 1,000-1,500
  tris/house; budgets) + new section 13.3. Tours done: stops 2-4 equal, stop 5 split (688/729 vs 584/605) -> jobs34.sh
  (stop 5 alone, nt_dev27 then nt_dev24; status in jobs30.status, "jobs34 done"). tb4 KILLED by me (memory 354 MB free
  with 4 other agents' games; my cc1plus 900 MB); requeued as /tmp/city/tb4b.sh (-O1, gate 2500, after jobs34).
  snap_b18.sh RAN: 7 files copied into the shared tree (blobs /tmp/city/b18_blobs.txt, all = dev28, in the store);
  MinGW in the shared tree waits for memory (-> /tmp/city/snap_b18.out, syntax_shared_b18.log). Then send
  /tmp/city/msg/b17_draft.txt (fill {{SYN}} {{FILES}} {{TOURS}}), then the doc (msg/doc_draft.txt: {{BLOB}} {{LINES}} {{TB4}}).
- ~21:45: BATCH 17 (beacon sync world side) SIGNED OFF to the lead (/tmp/city/msg/b17_final.txt): 7 files in the shared
  tree (b18_blobs.txt) + worldtypes.h 63a9e6e3; MinGW shared 0/0. Owed: stop-5 standalone numbers (both builds).
- ~21:40 the lead's MEMORY HOLD: nothing new until PID 14554 (sh /tmp/upd/gate.sh 2500) is gone or 22:30 UTC. jobs34 and
  tb4b were stopped before starting anything; /tmp/city/jobs35.sh (background) holds, then tb4 (-O1) and stop 5 on
  nt_dev27 then nt_dev24 (status in jobs30.status; "jobs35 done"). Compare: grep "autoplay tour shot\|gfx (frame"
  /tmp/city/tour5_dev2{7,4}_log.txt (or tourcmp.py tour5_dev24_log.txt tour5_dev27_log.txt).
- Doc: consistency fixes (worldc stores navmeshes, rebuilds meshes/collision at load; splat at load; sizes ~70 MB
  compiled / 20-30 MB compressed; startup ~4 s). Send the doc after tb4's breakdown (or without it).
- ~21:44 the lead: BATCH 17 ACCEPTED (snapshot 56 with the night agent's half + faces batch 10). Held items stay held; drop
  them for good once the hand-built world takes over. Stop-5 games not needed; run only prototypes the proposal needs.
  jobs35 stopped while holding (nothing had started); tb4 dropped.
- ~21:46: PROPOSAL SIGNED OFF to the lead (/tmp/city/msg/doc_final.txt): docs/HANDBUILT_WORLD.md blob
  4438e2f8426a4e2cec55f924f853cc33bdb9ed3e (1,133 lines; /tmp/city/doc_blobs.txt), in $S/hand0 only (shared tree has no
  docs/). OPEN: section 12 + road records in 4.4 when the AI agent's requirements arrive (after its set 18); re-sign then.
  NOTHING of mine is running.

## STAGE 0 (the lead, ~21:50): doc v1 changes + start stage 0 as WORLD ENGINEER
The lead approved D1-D7 (the user may override). Doc changes asked: (1) "Machine budget" subsection (14.3 GB cap, ~4 Wine
games or ~6 compiles; content roles on host tools; run budget per role; agents sustained; what a 2nd machine buys) - DONE
as 11.3 (Risks -> 11.4), plus 4 cores noted; (2) AI requirements into 12 + 4.4 when they arrive - OPEN; (3) 10.2 showcase
switches to hand mode once the pilot renders - DONE. Status line + section 14 "Decisions" (approved) updated. Re-sign the
doc when the AI requirements are in; the lead commits it then.
Stage 0 order: (1) terrain bake as data; (2) --world=hand loading terrain only from the embedded resource; (3) NTW
parser, formatter, worldc skeleton with embedding; (4) format spec frozen at v1. Small buildable sign-offs, procedural
mode byte-identical. The lead is lining up a tools engineer (parser, formatter, validator, previews) and an
interiors/runtime engineer; will say who.
- Private tree $S/s0 = HEAD 1a83e24 + batch 17 (dev28's 8 files) + my stage 0 work.
- Step 1 files: src/world/sites.h/.cpp (gTerrainBakeLandforms: layout runs only layoutPort + layoutKeyCoral, drops what
  they place), src/world/gridcodec.h/.cpp (NTG: MED predictor + adaptive Golomb-Rice + run mode, JPEG-LS-lite; unit test
  $S/s0test/gridtest.cpp ALL OK), tools/terrainbake.cpp (bake -> world/terrain/{terrain.ntw,height,water,region,coast.ntg},
  round-trip verified). Job /tmp/city/s0a.sh (status /tmp/city/s0.status) then /tmp/city/cq31.sh (tree $S/s0step1,
  worldcheck vs dev28.txt, clang scan of terrainbake unity, MinGW game + gridcodec).
- Step 2 files written (untested): src/world/ntw.h/.cpp (parser), worldbin.h/.cpp (container NTWB), handworld.h/.cpp
  (WorldKind inline var, selectWorldKind, embeddedWorldBin via FindResourceA "NT_WORLD" RCDATA, loadHandTerrain,
  loadHandWorld: empty roads (buildHash) + buildings (cellLists sized)), cellgen.cpp (early return in hand mode),
  tools/worldc.cpp (terrain.ntw -> build/gen/world.bin + world.rc). NEED PERMISSION: src/game/app.cpp (select kind, loader
  branch, skip game.init + stay in freecam in hand mode), build.sh/build.bat (worldc + windres/rc + link), src/main.cpp
  (include gridcodec/worldbin/handworld .cpp).
- ~22:45: STEP 1 SIGNED OFF (/tmp/city/msg/st1_final.txt): shared tree has sites.h d64ecce2, sites.cpp 73856240,
  gridcodec.h 48045818, gridcodec.cpp d9d08188, tools/terrainbake.cpp 9f1ac4ac, world/terrain/{terrain.ntw c59416c8,
  height.ntg 4e1f10f7, water.ntg 157f194e, region.ntg 030f4884, coast.ntg b07c108c} (/tmp/city/st1_blobs.txt); HEAD
  4e48029 (snapshot 56 = batch 17 + faces 10). MinGW shared 0/0; worldcheck identical (timings masked).
- The lead APPROVED step 2's edits with conditions: app.cpp on 17333c63, only my 3 hunks behind isHandWorld(), no merging
  (the lead 3-way merges; autoupdater + AI set 18 also edit it); main.cpp includes next to world/interiors.cpp; build.sh/
  build.bat additions on their own lines (WORLD_OBJ / WORLD_RES variables; I inserted `$WORLD_OBJ \` as a new line in
  build.sh's link command, and appended %WORLD_RES%/%WORLD_OBJ% to build.bat's two link lines); MSVC worldc modelled on
  embed_shaders, each command `|| exit /b 1`; procedural default and unchanged; smoke tests drive + melee must pass; show a
  --world=hand --shot of the bare terrain. Step 2 chain: /tmp/city/cq32.sh (status s0.status) then jobs36.sh (games).
- Step 3 work in $S/s1 (= s0 + step 3): ntw.cpp comments + formatter, ntwschema.h/.cpp (terrain kind so far), lz.h/.cpp,
  worldbin addCompressed/bytes (WSK_LZ), handworld via bin.bytes, main.cpp + lz.cpp, worldc framework (discover all
  world/**/*.ntw, schema check, manifest section, --check=), tools/ntwfmt.cpp, terrainbake writes canonical text.
  Tests: $S/s0test/ntwtest.cpp. Chain /tmp/city/cq33.sh (status /tmp/city/s1.status).
- Step 4 (spec): $S/s1/docs/NTW_FORMAT.md drafted (syntax 1-7 + kinds 9.1-9.13; roads/signal DRAFT pending the AI agent).
  Schema code for the kinds beyond terrain still to write in ntwschema.cpp (must match the doc).
- Doc v1 re-sign TODO (with the AI requirements): update HANDBUILT_WORLD.md section 4 examples to NTW_FORMAT v1: 4.4
  offset=12.0, connect <edge>:<lane>; 4.5 tree kind=..., gate=<at>:<width>; 4.6 option porch choices=... default=...,
  no $roof/$palette placeholders, furnish/variant/item variant=c (no '|'); 4.7 place plan=..., full palette ids,
  slot f0.u1 (slots are u<N>); 3.2 terrain files are .ntg (height 2.30 MB etc.); 6.2 terrain ~2.7 MB compressed.
- ~22:58: the lead ACCEPTED step 1 (snapshot 57, queued behind showcase set 4; step 2 can join it if signed off before 57
  starts). Step 2 host checks done (worldc, windres, handload ALL OK, clang, MinGW 0/0); nt_s0.exe build waits for memory
  (build.sh at flock 7); then jobs36 (hand shots + smoke drive/melee). Snap script /tmp/city/snap_st2.sh (guards HEAD ==
  1a83e24 for the 5 changed files, app.cpp 17333c63; new files absent). Sign-off draft /tmp/city/msg/st2_draft.txt.
  NOTE: worldc.cpp got a warning-only fix (LayerSpec initializers) after the tested build: rebuild worldc (-Wall -Wextra)
  and check world.bin is byte-identical before signing off.
- Step 4 tree $S/s2: full schema written (all kinds), NTW_FORMAT.md aligned (look= for mesh variants, furnish/variant/item
  variant=, class list = BuildingStyle names + landmark, building header id, gate <at>:<width>, name type). Samples per kind
  in $S/s0test/samples; chain /tmp/city/cq34.sh (status /tmp/city/s2.status).
- HANDBUILT_WORLD.md (hand0): examples in 4.4-4.7 now NTW v1; 3.2 bake table = real .ntg files; 6.2 terrain size; 4.2
  blocks list. Still to do for the re-sign: section 12 (AI requirements).
- ~23:35: STEP 2 SIGNED OFF (in-game runs pending) (/tmp/city/msg/st2_final.txt, blobs /tmp/city/st2_blobs.txt): HEAD
  0c288db (PR #49 merge). app.cpp 58a49778, main.cpp 505fb948, build.sh 81828550, build.bat 0bdcde10 NOT copied (the
  lead merges); cellgen.cpp ba81842d copied; new: ntw.h 99949624, ntw.cpp 5814989d, worldbin.h 28e6ab2f, worldbin.cpp
  6dce0152, handworld.h 3ab407ad, handworld.cpp 54e80ed8, tools/worldc.cpp 490d0c66. MinGW shared 0/0.
  OWED: jobs36 results (hand shots /tmp/city/s0hand, smoke drive/melee logs /tmp/city/s0smoke_*_log.txt).
- Step 3 ($S/s1): host checks all pass (cq33 + cq37 after the schema F() fix). cq36 builds nt_s1.exe after jobs36,
  then 1 hand shot (status /tmp/city/s1.status). Step 3 sign-off after that.
- Step 4 ($S/s2): samples per kind clean + canonical (cq37), tests/world/world_format_test.cpp (-Werror ok, passes, clang 0).
- ~23:40 (after compaction): the lead's STEP 2 REVIEW asks: (1) build.sh race: temp + mv -f, skip world steps when
  build/gen/world_res.o is newer than worldc and everything under world/ (build.bat needs neither); (2) hand-mode shots
  must show NO vegetation: turn off GPU grass + decor when isHandWorld(), say where, check in the 4 shots, send one shot;
  (3) nit: hash from byte 24 (covers the table). Then "send a fresh blob list when the runs are done".
  Done in $S/s0b (= HEAD 0c288db + step 1 + step 2 + asks): renderer.cpp (base 0d38d93): include ../world/handworld.h,
  decorIn.ready && !isHandWorld() (+comment line), grass->place and grass->draw behind !isHandWorld(); build.sh worldc and
  windres through .$$ temp + mv -f (two lines each so set -e catches a failed g++), skip test; worldc writeFile temp +
  rename; worldbin hash over out[24..] (finish + open). Chain /tmp/city/cq39.sh (status /tmp/city/s0b.status): worldc ok,
  handload ALL OK (damaged table byte 44 refused by the hash), MinGW 0/0, nt_s0b.exe built (15.6 MB), skip test ok; then
  4 hand shots (/tmp/city/s0hand) + smoke drive/melee (/tmp/city/s0smoke_*). renderer.cpp = MERGE-ONLY blob (render agent's).
- Step 3 rebased: $S/s1b = s0b + step 3 (merge-file; conflicts in build.sh WORLDC_SRC and worldc includes resolved).
  Step 4 rebased: $S/s2b = s1b + schema/docs/tests. OLD s1/s2 are references only now.
  New in step 3: ntw.cpp formatNumbers formats ranges and sizes (8-18 -> 8.00-18.00, 2.1x0.9 -> 2.10x0.90);
  tests/world/world_format_test.cpp moves INTO step 3 with only the terrain sample (kSchemaKinds=1); step 4 embeds 15
  samples (kSchemaKinds=17). Generator: $S/s0test/embed_samples.py <test> <samples dir> (samples3 = terrain only).
  Doc examples checker: $S/s0test/doc_examples.py <outdir> <md files> (-> $S/s0test/docex, ntwfmt --check it).
  Chain /tmp/city/cq40.sh (status /tmp/city/s1b.status): s1b + s2b worldc/ntwfmt/world_format_test with CI's GCC flags
  (UBSan) and timings, clang scans, samples + doc examples --check, s1b handload, s1b MinGW syntax.
- AI requirements folded: schema (s2b ntwschema.cpp: control none|stop|yield|signal, moves at+lanes (no dir), hours/ages/
  sizes formatted), NTW_FORMAT.md (9.3 zones, 9.4 roads+signal out of draft, 9.5 plaza/path/steps/spot/lot/row/ramp + spot
  kinds, 9.8 room cap/door revolving+hours/stair treads+rise/lift cap+call/spot, 9.11 anchor, section 3 compounds,
  section 4 hours), HANDBUILT_WORLD.md (status v1; 4.2 blocks; 4.4 new example + signal plan; 4.5 plaza/spot/lot; 4.6
  spot/room cap; 4.8 table; 7.5; 7.7 events; 9.1 checks; section 12 rewritten 12.1-12.7). Backup of the doc before:
  $S/hand0/HANDBUILT_WORLD.md.before_ai. Pushback list = 12.7 table (send to the lead with the re-sign).
- ~00:15: cq39: 4 hand shots OK (no vegetation; PNGs /tmp/city/s0hand/*.png, sheet /tmp/city/s0hand_sheet.png), smoke
  drive rc 0 / 0 fatal / Shutdown complete. Melee pending. cq40 (s1b/s2b) ALL PASSED: world_format_test CI flags build
  12.0 s (s1b) / 13.7 s (s2b), run 0.5 s; samples 15 + doc examples 9 clean+canonical; doccheck 0 real problems.
  cq41 failed: rc 126 = s1b/build.sh lost its +x in the merge (fixed in s1c/s2c with chmod +x).
- ~00:28: LEAD: snapshot 57 committed (c9ff700, merge 2b3102e, PR #50: terrain bake + AI set 18 + heel pivot). app.cpp at
  HEAD is AI's 6f3ee5da: send step 2's app.cpp as a fresh blob on 6f3ee5da. main.cpp, build.sh, build.bat unchanged at
  HEAD. NEW ASK (step 2): CRLF on Windows CI: ntw.cpp must treat \r\n as a line end (test on a CRLF copy of terrain.ntw)
  AND add .gitattributes (`*.ntw text eol=lf`, `*.ntg binary`). Both done:
  $S/s0c = HEAD 2b3102e + step 2 (reviewed) + .gitattributes; app.cpp merged clean = 2d0cb21a. The parser already strips
  \r before \n and a BOM: worldc (step 2) on CRLF and BOM+CRLF copies -> world.bin byte-identical ($S/crlftest).
  Attributes simulated with autocrlf=true/eol=crlf in $S/attrtest: .ntw stays LF, .ntg byte-identical.
  $S/s1c = s0c + step 3 (+ worldc textHash: manifest hashes LF text without BOM; world_format_test CRLF+BOM case);
  $S/s2c = s1c + step 4 (+ NTW_FORMAT.md section 1 notes .gitattributes). Chain /tmp/city/cq42.sh (status
  /tmp/city/s1c.status): s0c/s1c MinGW syntax, s1c/s2c worldc (LF/CRLF/BOM identical), ntwfmt, wft CI flags timed, clang,
  samples/docex/doccheck, handload, nt_s1c.exe, then 1 hand shot after cq39.
  Step-2 snap script must be REWRITTEN for HEAD 2b3102e (+ .gitattributes new file, app.cpp blob on 6f3ee5da).
- ~01:00: SENT doc v1 re-sign (b6d50c00, 86,965 B) + 13 pushbacks. LEAD: doc v1 ACCEPTED, in snapshot 58 (with faces
  batch 11; 58 starts when showcase set 0 ends); the lead put it in the shared tree as docs/HANDBUILT_WORLD.md. Step 2's
  final blobs ride in 58 if they reach the lead before set 0 ends, else 59 ("don't rush the runs").
  AI agent AGREED to all 13 pushbacks + details -> doc v1.1 (land with step 2 or 3; v1 stays in 58): done in
  $S/hand0/docs/HANDBUILT_WORLD.md = blob 4f2153fb (89,830 B, 1,399 lines; v1 backup HANDBUILT_WORLD.md.v1_b6d50c00):
  status v1.1; 7.5 entrance links door->gate->walk node; 9.1 placement (no link across fence/wall) + roads (no
  overlapping carriageways ~10 m outside boxes, Sol Beach 5050,490 edges 3287/3284 node 2478; no conflicting greens
  except yielding permissive turns; one leg per compass sector without approaches=; walk links don't cross fences);
  12.2 item 5 + new item 12; 12.3 clear width per stretch+side, colliders real size incl. height; 12.4 zones override the
  region grid inside polygons once any exists; 12.5 door state/occupancy calls O(1) deterministic, polled ~1/s within
  ~150 m, furniture collider heights; 12.6 list; 12.7 rows. NOT copied into the shared tree (v1 stays in 58).
  s2c NTW_FORMAT.md: zone override note + collide heights note. DON'T start the roads.cpp overlap fix (lead will ask after
  AI set 19).
- cq42/cq43 results: s0c + s1c MinGW 0/0; s1c/s2c worldc 0 warnings, ntwfmt ok, wft CI flags s1c 7.0 s build / 0.4 s run,
  s2c 8.9 s / 0.3 s (earlier s1b 12.0/0.5, s2b 13.7/0.5 under more load); clang 0; samples 15 + docex 9 clean+canonical;
  CRLF and BOM+CRLF -> identical world.bin (s1c, s2c); world.bin step3 == step4; s1c handload ALL OK; nt_s1c.exe built
  (15,650,304 B). cq42 waits for cq39 (melee) then 1 hand shot (/tmp/city/s1hand).
- ~01:20: LEAD: the animation agent's stair/door spec ($S/hand0/stair_door_spec.txt) -> doc v1.1: kit defaults, worldv
  errors for hard limits (tread <0.25 at walking line, walking line <0.5, landing < flight width or <0.9, door <0.8 clear,
  corridor/landing <1.0), soft = warnings. DONE: new 4.9 table + notes, 9.1 errors/warnings, 12.5 pointer; status v1.1.
  s2c format: stair kinds +quarter|winder|curved|escalator, landing=, walk= (from centre on spirals/curved, inner corner
  on winders), door w = clear width; sample $S/s0test/samples/layout_core.ntw (16 samples). cq44 all pass (wft CI 11.3 s
  build / 0.5 s run). Doc v1.1 blob 6578a8ca (93,587 B, 1,435 lines).
- ~01:35: melee smoke PASSED (rc 0, 0 fatal, to t=18, attack moves, health 188). snap_st2c.sh RUN: shared tree now has
  worldbin.h c4684187, worldbin.cpp 760e13f3, worldc.cpp fe8cab97, .gitattributes 07bcc6e7 (new); merge-only blobs:
  app.cpp 2d0cb21a (on 6f3ee5da), main.cpp 505fb948, build.sh 05570425, build.bat 0bdcde10, renderer.cpp 3a970bc0.
  Shared MinGW 0/0. SENT step-2 final (/tmp/city/msg/st2c_final.txt) incl. doc v1.1 blob + the 1.0 m reading to confirm.
  NEXT: s1c hand shot (cq42), then step 3 sign-off AFTER step 2 is committed (58 or 59): rebase s1c on that HEAD,
  copy step-3 files with guards, blobs for main.cpp/build.sh (on HEAD's versions), world_format_test command + runtime.
  Then step 4 (s2c) sign-off.
- ~01:45: LEAD: step 2 ACCEPTED as snapshot 59 with doc v1.1 (6578a8ca); 59 starts after 58 (~02:00-02:15); lead copies
  nothing now; after 59 commits the lead puts app.cpp, main.cpp, build.sh, build.bat, renderer.cpp and the doc in the
  shared tree; then sign step 3 off ON THAT HEAD. 1.0 m turning rule reading AGREED (corridors + dead-end landings only).
  s1c hand shot OK (/tmp/city/s1hand/s1_hand_downtown_aerial.png; terrain from the exe 2606 KB, 6 sections, 1.82 s).
- LEAD: the autoupdater is folded into 59 too: app.cpp, main.cpp, build.sh, build.bat are 3-way merges (build.sh link:
  `$WORLD_OBJ \` then `-lavrt -lwinhttp -lbcrypt`; build.bat cl `%NTVER%`, `winhttp.lib bcrypt.lib %WORLD_RES%`; MinGW
  `-lwinhttp -lbcrypt %WORLD_OBJ%`; new src/core/version.h, VERSION= option). Base step 3 on 59's HEAD:
  /tmp/city/prep_st3.sh <head> builds $S/s1d = git archive HEAD + step 3 (main.cpp/build.sh merge-file onto HEAD's).
- ANSWERED the lead: YES to the updater agent as worldv tools engineer now. Boundary: theirs = tools/worldv.cpp,
  src/world/validate*.{h,cpp}, tests/world/worldv_test.cpp + fixtures, the CI step; mine = ntw, ntwschema, gridcodec, lz,
  worldbin, handworld, worldc, ntwfmt, terrainbake, NTW_FORMAT.md, HANDBUILT_WORLD.md, and the future typed world model
  src/world/worldmodel.* (they request API; no second model). First slice: framework (discovery, parse/schema/canonical,
  diagnostics with stable ids, errors vs warnings, summary, --only, incremental), ids/references, per-layout 4.9 stair/
  door limits + 9.1 library checks, CI step now. Later: roads/AI checks (stage 1). Start material: $S/s2c + samples.
- ~02:05: snapshot 58 committed (f595703 merge of PR #51: faces + doc v1). 59 (step 2 + updater + doc v1.1) next.
- ~02:20: worldv engineer (agent a007b71eaee91d282, ex-updater) asked: (1) CI step for world_format_test (answered: their
  command exactly, name "Test world file formats (NTG grids, NTW parser, formatter and schema, LZ, world.bin)", timeout
  10 OK); (2) schemaRefs() table: ADDED to s2c ntwschema.h/.cpp (their 66 rows + 21 per item kind, plus override remove
  <f.wall|f.door|f.window|f.light|f.spot|f.counter|f.sign> and indoor spot for <.door|.counter>; lift door -> word
  front|back|left|right); world_format_test testRefs(); (3) id scopes/patterns/paths confirmed; NTW_FORMAT section 5 now
  has scope paragraph + rows (zone/anchor <d>.<name>, plaza/lot/spot <block>.<name>, transit <kind>.<name>, numbered
  parts "at least" digits; world/library pattern misses errors, element misses warnings). cq45 all pass (wft 10.1 s /
  0.3 s). Their files: tools/worldv.cpp, src/world/validate*, tests/world/worldv_test.cpp, CI step.
- ~02:19 CONTAINER RESTART (all processes killed; disk survived). The lead briefly started a replacement world engineer
  (read-only, stopped); I remain the world engineer. New agent ids: AI a7a56a6b5fceb19ac, animation a5439fc9aa2dad40e;
  NEVER message the old ids (a618240ba5b5e29a8, a4c45196eb6f5d828); prefer going through the lead.
  59 (tree e27fe1da) died in smoke tests at the reboot; re-running. After 59 commits the lead copies its files into the
  shared tree and tells me; then step 3 sign-off on that HEAD (/tmp/city/prep_st3.sh <head>), then step 4.
- SENT road-overlap estimate (31 pairs >= 12 m in $S/ts/overlaps.txt): cause = named arterials added on top of district
  grids, planariser (roads.cpp ~1013-1210) never checks meeting angle/width overlap; fix = pre-planarise corridor clearing
  (~100 lines: minor road stretches within hwA+hwB+1 m at <30 deg cut, pieces <20 m dropped, T-snap reach hwA+hwB+2 for
  cut ends), risk medium/local, 6-7 agent-hours. DON'T START without the lead's go.
- ~02:46: snapshot 59 COMMITTED as d208e02 (step 2 + doc v1.1 + autoupdater; PR #52, CI running = first MSVC test of
  worldc/rc; if MSVC fails on build.bat/worldc the lead will ask for a fix). Shared tree = d208e02 (+ AI's traffic_sim.cpp).
- STEP 3 SIGNED OFF on d208e02 (/tmp/city/st3_blobs.txt): $S/s1d = git archive d208e02 + step 3 (main.cpp/build.sh merged
  onto 59's). 6 new standalone files COPIED into the shared tree (lz.h/.cpp, ntwschema.h/.cpp step-3 versions, ntwfmt.cpp,
  tests/world/world_format_test.cpp step-3 version); 10 changed files BLOBS ONLY (main.cpp 96b7c6cd, build.sh 23675c45,
  ntw.h a19d9d2d, ntw.cpp 16f5ae39, worldbin.h 6495e861, worldbin.cpp 23699c8a, handworld.cpp 07589e1b, worldc.cpp
  46563ddf, terrainbake.cpp f95b26b0, terrain.ntw 76451f1a): must land together. Shared MinGW 0/0. cq46 all pass.
- STEP 4 SIGNED OFF (blobs only, /tmp/city/st4_blobs.txt): $S/s2d = s1d + step 4: ntwschema.h 862eac69, ntwschema.cpp
  b6339abb, world_format_test.cpp b91a548e, docs/NTW_FORMAT.md 1785aebf (new). f.sign dropped from override remove.
- LEAD GO for the road-overlap general fix (roads.cpp only, own snapshot-safe sign-off) AFTER steps 3-4; then the world
  model. Checks: worldcheck before/after (counts + buildings moved within 150 m of each spot); every computePlaces place on
  the same building and door (a moved mission place is a BLOCKER: tell the lead first); AI's traffic_sim --overlaps 31 -> 0
  and deadlocks at 281-288 and 2478 gone (build traffic_sim from the shared tree's tools/traffic_sim.cpp, don't edit it);
  drive + melee smoke; shots of 3-4 changed corners incl. Bayshore/9th Ave and Sol Beach; bus 12 may re-route but must
  serve its stops (say which change).
- ~03:00: STEPS 3+4 ACCEPTED as snapshot 60 (17 files), chained after showcase set 1. 59 merged as 837b39c.
- worldv slice-3 conventions ANSWERED (to fold into a small "step 4.1" follow-up AFTER 60 commits): door/window `at` =
  opening centre; fits wall margins (half meeting wall t + 0.05); joins two rooms (t/2+0.05 test points); `out` only via
  ext= in layouts; slot entry doors name the slot (leads row gains .slot); units/fitouts `out` = outside slot_size rect.
  Stairs: run = flight CENTRELINE; dogleg 4 pts (p1,p2 at landing mid-depth; flights = seg - landing/2); quarter 3 pts
  (p1 landing centre); winder 3 pts; spiral/curved arc fitted, sweep may pass 360; tread at walking line = walking-line
  flight length / treads (spiral: sweep x walk radius); landing default = w; walk default centreline radius (spiral) or
  w/2 (winder). SAMPLE FIX: layout_core st2 -> treads=12 rise=0.22. Door-at-stair landing check (flight ends in front of
  the door within w +-0.3, >= 0.9 from wall face); turn-behind-door warning (w<0.9 and free depth <1.2); corridor AND hall
  < 1.0 error; dead-end landings later; outdoor rooms exempt from closed check; hatch access counts at plan level.
- ROAD FIX design (in progress): edge-level pass AFTER the per-edge attributes loop (seeds assigned) and BEFORE Elevation,
  preserving indices/seeds (new edges fill freed slots first; new seeds derived from parent seeds; nodes appended only).
  Minor stretch beside a winning road (rank: hwy0 blvd1 ave2 rural3 street4 lane5 dirt6; bridge wins ties; then longer
  road by name; then lower index) within hwA+hwB+1 at <30 deg (nearest point on A must be interior, not A's end) is cut;
  connector from a new node M on A (split A) to the cut point P at ~65 deg; dangling pieces <20 m dropped.
  Checks harness plan: worldcheck-like census (edges, buildings by position, bus routes/stops) + replicated computePlaces
  resolvers; traffic_sim from the shared tree's tools/traffic_sim.cpp copied into my tree (TS_ROADS_CPP option exists).
- ~03:30 ROAD FIX in progress. Tree $S/rf/t (= git archive 837b39c + AI's set-19 traffic_sim.cpp copy + src/world/
  roads_base.cpp = HEAD's roads.cpp for baselines). The pass is in rf/t/src/world/roads.cpp before "Elevation"
  (draft also in $S/rf/overlap_pass.inc, older). Round 1 (hwA+hwB+1 criterion): 237 cuts, too broad. Round 2 (deep
  <=0.8*max hw, >=12 m): 94 cuts. Round 3 (deep counted only outside junction cut-backs = node radius estimate; leftover
  slots CLOSE UP in order instead of swap-with-last, which moved far edges and changed ~25 far buildings): chain
  /tmp/city/cq50.sh (status /tmp/city/rf.status): ts_fix, --overlaps, census (rfcheck.cpp in $S/rf; diff rfdiff.py ->
  /tmp/city/census_diff.txt), sims mid/beach seeds 1-3 base vs fix (/tmp/city/sim_*). Base binaries /tmp/city/ts_base.
  Round 2 result: overlaps 31 -> 0; places? (census P lines); buildings changed 119/100, 27/23 beyond 150 m (from
  swap-with-last + long cut edges). NEXT: exe build (OUT=/tmp/city/nt_rf.exe in rf/t), smoke drive/melee, 3-4 shots
  (Bayshore/9th Ave ~3900,3450; Sol Beach 5050,490; + 2 more), then sign-off (roads.cpp only).
- worldv engineer: answered 4.1 sample completion (layout.ntw closed living; layout_core walls w6/w7, opening d3, st1
  0.75,6.00;0.75,9.20;2.25,9.20;2.25,6.00 treads=18 rise=0.16 landing=1.10; st2 treads=12 rise=0.22) and the stage-1
  plan (worldmodel v0 after the road fix; LaneGraph called by worldv itself; F via the lead; plan pass may start now).
- ~03:40 CONTAINER RESTART (2nd). LEAD: resume road fix, then 4.1, then world model; one game at a time; snapshot 60 (steps
  3+4 + worldv slices 1-4 + CI) chained behind the showcase; HEAD still 837b39c.
- ROUND 3 RESULT (cq50, sims killed by the restart): overlaps 31 -> 2 (Venetia Cswy/48th St 12 m; Caribe/Old Cutler 12 m:
  my 12 m deep threshold at 2 m steps is stricter than the AI's 3 samples x 4 m), but the leftover-slot CLOSE-UP shifted
  6249 edge indices -> 6157 buildings changed HEIGHT: buildings.cpp:323 b.face = ei*2+side feeds blockstyle.cpp:1077
  hash (also transit.cpp:1058 shelter seed by ei, population.cpp:962 parked side by ei). => NO index may move.
  Also found: R3 left 5 m dead-end stubs inside Bayshore (11th Ave: edges 176, 179; 9th Ave: 280) and DISCONNECTED the
  Flamingo Causeway (9247 ends at node 5934 on 9th Ave, 5.4 m inside Bayshore; 9th Ave edges there were dropped).
  Shared pieces 182 (11th Ave 174-175) and 282 (9th Ave 287-288) carry Bayshore's path with the street/avenue class
  (pre-existing, not touched; lane graph handles lane-count changes at degree-2 nodes; mention as follow-up).
- ROUND 4 (in $S/rf/t/src/world/roads.cpp, block also in $S/rf/pass_v4.inc; R3 copy $S/rf/roads_r3.cpp): piece model;
  threshold 8 m deep, kParallel 0.85, margins +1 both roads (to cover the AI metric); cuts extended to the edge end when
  < 20 m would be left, merged when < 20 m between; joins as R3 (65 deg); dangling pieces < 20 m at nodes the cuts took a
  road from go (iterative); absorbed junctions inside A's carriageway (d < hwA) move onto A (snap into A's end junction
  within radius+5 m or 3.5 m) unless a < 20 m stub joins them to A; dead ends outside get a link; slots: keeper = piece
  from the edge start (keeps seed), extras fill freed slots then append; DEFICIT: the piece nearest the gone edge's middle
  is split R/0.45+1 m (>= 12) short of its n1 junction (no lots fit; junction keeps its full cut), until no slot is free.
  Harness: $S/rf/rfdump.cpp (edges/nodes around points; /tmp/city/rfdump_base built). Chain /tmp/city/cq51.sh (status
  /tmp/city/rf.status): ts_fix, --overlaps --deadends all for base (ov_base2.txt) and fix (ov_fix4.txt).
- ~04:30 LEAD: snapshot 60 COMMITTED as 2c80a38 (steps 3+4, worldv slices 1-4, CI steps; PR #53, CI running = first run of
  the new GCC steps; if the GCC world-format step fails the lead comes to me). Shared tree = HEAD except AI's
  tools/traffic_sim.cpp (3bf2159a). Base the roads fix AND 4.1 on 2c80a38. roads.cpp at 2c80a38 = 4569edfb (unchanged);
  snapshot 60's src changes don't touch world generation. NEW fix tree: $S/rf/t2 = git archive 2c80a38 + traffic_sim
  3bf2159a + roads_base.cpp (4569edfb) + my roads.cpp. Work in t2 from now on (t = 837b39c, same world-gen code).
- ROUND 4 results (cq51): overlaps 31 -> 0; dead ends 1255 -> 1255 (same positions); 32 cuts (2684 m), 7 joins, 1 junction
  moved onto (Flamingo Cswy node 5934), 4 hanging stubs dropped, 29 edges gone, 21 deficit splits, 0 appended; network
  6251 nodes / 9547 edges / 1680 signalized (base 6223/9547/1690). Census + sims: /tmp/city/cq52.sh (census_fix4.txt,
  census_diff4.txt, sim_*_fix4_*.txt). Exe/smoke/shots: /tmp/city/cq53.sh (t2; shot list /tmp/city/rf_shot_args.txt;
  base shots on nt_s1c.exe = same world).
- ~04:55 LEAD ACCEPTANCE BAR for the road-fix sign-off (keeping indices + junction-pair merge = "good calls"):
  (1) no district (Midtown, Sol Beach) worse than base on waits >60 s SUMMED OVER SEEDS 1-3 (not just the old nodes);
  (2) overlaps 0; (3) dead ends and the 52 story places unchanged; (4) building count + buildings that moved, LISTED within
  150 m of each changed spot; (5) bus 12's changed stops listed. If a district is still worse: say which and why; the lead
  decides whether it ships.
- R4 sims: mid fix seed1 36 (base 23), beach fix seed1 67 (base 15; 2478 itself 47 -> 1): jams moved to junction pairs
  left 5-11 m apart (2544/2546 Bay Rd x 35th St/Solano; 281/283) + spillback through deficit-split nodes.
- R5a (alongTo extension: rest of a cut road < 40 m running beside A goes; deficit splits only before deg>=3 junctions):
  overlaps 0, 32 cuts, 6 joins, 30 gone, 23 splits ($S/rf/roads_r5a.cpp). R5b (deep = d <= max(hwA, 0.8 hwB); absorbed
  node with a < 20 m stub to an A junction MERGES into it, stub dropped; deficit splits prefer streets/lanes, +150 m
  penalty for avenues and up): overlaps 0, dead ends identical, 37 cuts (3019 m), 10 joins, 5 merged/moved, 5 hanging,
  36 gone, 25 splits; 6258 nodes / 9547 edges / 1679 signalized. Chains: cq54 (census r5b: census_baser.txt,
  census_fix5r.txt, census_diff5.txt), cq55 (sims base + fix5, seeds 1-3; sim_*_fix5_*.txt).
- ~05:15 CONTAINER RESTART (3rd): all my chains died (cq55 sims mid-run, cq56/cq57/cq53 queued). HEAD = bcb8a71 (merge of
  PR #53 = 60; tree identical to 2c80a38: 21e3bfc7), shared roads.cpp still 4569edfb, traffic_sim 3bf2159a. t2 is valid.
  Survived R5b sims: mid fix5 s1 0 / s2 1 (base 23 / 28); beach fix5 s1 27 (2264 head-on hold on Ocean Promenade edge
  2914, unchanged edge) / s2 0 (base 15 / 20). R5c (= 5b + doubled-up roads between the same two junctions dropped, e.g.
  48th St 3350 vs Venetia 3308 after the 2561->2559 merge) is in t2 roads.cpp, not built yet. Relaunched: cq58 (R5c
  ts_fix6/census/report + sims, base and fix streams in parallel) and cq53 (exe, syntax, smoke, shots after the census).
- ~05:50 ROUND 5d = FINAL CANDIDATE (5c with the doubled-up rule limited to junctions the pass changed; R5c had also
  dropped 3 pre-existing duplicates far from any spot). Copy: $S/rf/roads_r5d.cpp (+ comment-only header update in
  t2). BLOB c5bc0aab (174,750 B, +667 lines, on HEAD bcb8a71's 4569edfb; shared copy still 4569edfb, NOT copied).
  overlaps 31 -> 0; dead ends 1255 identical; LOG: 37 stretches (3019 m), 10 joins, 5 junctions moved/merged, 5 hanging +
  2 doubled dropped, 38 gone, 27 slot splits, 0 appended; network 6260 nodes / 9547 edges / 1679 signalized.
  Census: 87 of 9547 slots changed; buildings 39075 -> 39049 (159 removed, 133 added), 16 spots, 3 beyond 150 m (garages
  of the Almeria/Levee frontage block); story places 52/52, site places 15/15, interiors 29/29 identical; bus 12 8332 ->
  8570 m (south terminus loop one block longer: Bougainvillea's duplicate gone, loop via S 18th St), 20 stops, 15 shifted
  10-290 m along Bayshore (5 unchanged); bus 9: 2 stops moved 0.1 / 8 m; buses 3, 7 identical.
  SIMS (waits >60 s, seeds 1-3 summed): Midtown 68 -> 1, Sol Beach 92 -> 27 (all 27 in seed 1 at unchanged Ocean
  Promenade nodes 2264/2267/2261), Downtown/11th Ave 105 -> 19, Old Cutler 154 -> 34. Files /tmp/city/rf_signoff/.
  MinGW syntax (t2) rc 0, 0 errors, 0 warnings. Exe nt_rf.exe building via build.sh (cq53), then smoke + shots;
  base shots on nt_s1c.exe (cq60).
- ~07:00 road fix: smoke drive + melee PASSED on nt_rf.exe (t2 = bcb8a71 tree + roads c5bc0aab); base shots (4 corners,
  nt_s1c.exe) done in /tmp/city/rfshots_base; fix shots running (cq53 -> /tmp/city/rfshots_fix); official
  tools/worldcheck base vs fix queued (cq61 -> /tmp/city/wc_rf_{base,fix}.txt, diff wc_rf_diff.txt). Sign-off draft
  /tmp/city/rf_signoff/signoff_draft.txt (fill @SHEETS@ + worldcheck line), sheets via $S/rf/rfsheet.py.
- STEP 4.1 PREPARED in $S/s41 (= bcb8a71 tree): samples layout.ntw / layout_core.ntw = the fixture world's completed
  bungalow_b02_f0 / core_a minus their comment lines; ntwschema.cpp refs: door leads `out:<.room|.slot>|...`, stair
  from `<f.room|.room>`, to `<f.room>|+1:<.room>|-1:<.room>` (relative floors use ':' because worldv's pattern matcher
  splits parts at ':' and "+1.stair" is no id - TELL WORLDV: I promised `+1.<room>`, it is `+1:<room>`); NTW_FORMAT.md
  9.8: door/stair rows + "How the validator reads a layout's geometry" (openings, leads/slots/out, stair runs per kind,
  defaults, relative floors); world_format_test.cpp re-embedded (16 samples); worldv_test.cpp (THEIRS) want = {} for the
  samples - coordinate. Checks chain /tmp/city/cq62.sh (status /tmp/city/s41.status).
- ~07:35 ROAD FIX SIGNED OFF (snapshot-safe, roads.cpp only): blob c5bc0aab on bcb8a71 (shared copy NOT copied, still
  4569edfb). Message copy /tmp/city/msg/rf_signoff.txt; package /tmp/city/rf_signoff/ (report_spots_buildings_bus.txt,
  census_diff.txt, sims.txt, overlaps_deadends_{base,fix}.txt, sheets/, blob.txt). Bar met: every district better than
  base (Midtown 68->1, Sol Beach 92->27, Downtown 105->19, Old Cutler 154->34), overlaps 0, dead ends + 52 places
  identical, buildings listed per spot (3 beyond 150 m), bus 12 stops listed, smoke drive+melee pass, worldcheck passed
  both, 3 corner shot sheets (4th fix shot timed out under load). Await the lead's answer.
- NEXT: step 4.1 sign-off ($S/s41 ready, all checks pass: wft + worldv_test CI flags, worldv world 0/0, relative-floor
  stairs 0:/+1:/-1: clean in worldv + canonical in ntwfmt). Then tell worldv (format `+1:<room>` not `+1.<room>`;
  their worldv_test sample expectations -> {}). Then world model v0 (header draft to worldv first).
- ~07:45 STEP 4.1 SIGNED OFF (blobs only, /tmp/city/st41_blobs.txt): ntwschema.cpp 906d1d0f, NTW_FORMAT.md d318643d,
  world_format_test.cpp 90f983c5, samples layout.ntw c491f31b, layout_core.ntw 08c11cd6, worldv_test.cpp 99719d70
  (WORLDV's file: asked them to confirm with the lead). Relative stair floors = `0:<room>` / `+1:<room>` / `-1:<room>`
  (told worldv; their stair from/to parsing should take the room after ':'). Checks all pass (wft, worldv_test CI flags;
  worldv world 0/0; relative test layouts 0/0; ntwfmt canonical; doc examples 9; worldc 0 warnings, world.bin identical).
- NEXT: WORLD MODEL v0 (typed world model src/world/worldmodel.*; worldv requests API; no second model): send worldv a
  header draft first.
- ~08:00 worldv CONFIRMED the 4.1 worldv_test.cpp hunk (telling the lead) and the 0:/+1:/-1: relative floors.
- WORLD MODEL v0 HEADER DRAFT sent to worldv: $S/wm/src/world/worldmodel.h (tree $S/wm = bcb8a71 + 4.1 ntwschema.cpp),
  namespace World::Hand: Src{file,line}, File{path,kind,doc}, Ref{text,target,src}; typed District/Zone, Node, Edge (+
  LaneCount/LaneUse/NoSwitch/Moves/Crossing/BusStop/KerbStretch/CurbCut/Deck/Sign), Junction (StopLine/Connect/Ban/Head),
  SignalPlan/Phase, TransitLine (Station/Stop/Pier/Pole), Place, Anchor; Block/Building headers; LibItem; Model with
  index + find(cls,id) + where(src); load(dir, Model&, errors); RoadIndex; compileRoads(model, terrain, RoadNetwork&,
  RoadIndex&, errors). Syntax ok (host -Wall -Wextra, MinGW). Asked worldv for: fields to type in v0, Src vs path,
  who resolves refs. F = worldv calls LaneGraph::build itself. NEXT after their answer: write load() + compileRoads()
  (factor the finishing passes out of roads.cpp generate(): procedural must stay byte-identical) + tests (samples + a
  two-district fixture world through world.bin).
- ~07:35 (real clock) WORLDV REVIEW of the header (agent msg): shape good. Wants: (2) load() resolves refs WITH
  ntwschema's RefDef patterns (same as worldv ref.form/missing/kind); (4) split: discover(dir) (worldc's rules; worldv
  drops its copy) + loadParsed(std::vector<File>&&, Model&, errors) + load(dir) shortcut; (5) Error gets a short code
  ("road.node", "road.shape"); (6) Node::edges (incident edge idx) + Node::junction (-1); (7) stable order: tables in
  discovery (sorted path) order, records in file order; (8) read-only after load (const find ok, threads). Q9: terrain
  for compileRoads - may pass nullptr? Q10: does Edge::shape include both node positions (yes: fixture shows it).
  PLAN v0 (decided): ntwschema public ref-pattern API (parseRefPattern/matchRefs/refRules, same code as worldv's) ;
  src/world/roadnet.cpp = roads.cpp's pure core moved out (roadInfo, posAt/tangentAt, buildHash, edgesInRect,
  nearestEdge, junctionZ, onPavement, nearRoad, bulbRadius, totalLength + NEW RoadNetwork::finishJunctions() = the
  "Intersections: radius, cut-backs, control" loop), roads.cpp `#include "roadnet.cpp"` (precedent: buildings.cpp ->
  blockstyle.cpp); roads.cpp v0 based on c5bc0aab (road fix) ; worldmodel.cpp (loader, ntw+ntwschema only) +
  worldmodel_roads.cpp (compileRoads; needs roadnet.cpp + core/math.cpp); NO terrain param (heights absolute; grading =
  worldc stage 1); edge seeds from id hash; Node control CTL_AUTO default; median/lanewidth/shoulder default = class's;
  parcels/lots flat tables; Ref{text, classes, cls, target, src}. world.bin round trip -> stage 1 (tell worldv/lead).
  Tests tests/world/worldmodel_test.cpp + own fixture tests/world/fixtures/model/ (two districts, worldv-clean,
  canonical). Verify procedural identity after factoring (rfcheck census vs the fix census). Then sign-off + worldv reply.
- ~08:10 WORLD MODEL v0 WRITTEN in $S/wm (= bcb8a71 + 4.1 six files + roads.cpp c5bc0aab + v0; base copy of roads
  $S/wm_roads_base_c5bc0aab.cpp; old draft $S/wm_worldmodel_draft0.cpp). Files: src/world/worldmodel.h (API per worldv
  review: Src, File{path,kind,doc,valid}, Ref{text,classes,cls,target,src}, Node CTL_AUTO + edges + junction, Parcel/Lot
  tables, Error{src,code,message}, Def, Model{tables, ids, find, where}, discover, loadParsed, load, RoadIndex,
  compileRoads(model, rn, index, errors) NO terrain); worldmodel.cpp (Loader: typed readers via Ntw::refRules +
  matchRefs; DistrictPart attach after; resolveAll); worldmodel_roads.cpp (compileRoads: shape/profile checks,
  widths, flags, seeds=hash32(fnv1a(id)), highway flag, finishJunctions, authored control, buildHash); roadnet.cpp
  (moved from roads.cpp: kRoadInfo/roadInfo, posAt, tangentAt, NEW finishJunctions (the moved loop), buildHash,
  edgesInRect, nearestEdge, junctionZ, onPavement, nearRoad, bulbRadius, totalLength; roads.cpp keeps surfaceHeight,
  includes roadnet.cpp after <unordered_map>; diff = 194 lines moved + include + `finishJunctions();`); roads.h decl;
  ntwschema.h/.cpp: RefPart/RefAlt/RefPattern/parseRefPattern, RefName/matchRefs, RefRule/refRules (unordered_map);
  docs/NTW_FORMAT.md 9.4 "What the roads compiler reads, and requires"; tests/world/worldmodel_test.cpp (discover,
  samples, model fixture, compile numbers, errors with codes/lines, load on broken temp files) PASSED first build;
  world_format_test testRefs + pattern cases; fixture tests/world/fixtures/model (11 files, canonical, worldv 0/0, 33
  ids); .github/workflows/windows-build.yml + step "Test the world model (loader, references, roads compile)".
  CHECK CHAIN /tmp/city/cq63.sh (status /tmp/city/wm.status): wft, worldmodel_test, worldv_test (CI flags), worldv
  world + model fixture, ntwfmt, worldc (world.bin vs s41's), MinGW main.cpp + test, rfcheck census vs
  census_fix7r.txt. Scratch harness $S/wm/wmcheck.cpp (build after the chain). NEXT: chain results -> blobs
  (/tmp/city/wm_blobs.txt + apply script) -> sign-off to main (after road fix + 4.1) -> reply worldv with answers.
- ~13:31 CONTAINER RESTART (3rd). cq63 had FINISHED at 08:40 before it: all green (wft, worldmodel_test, worldv_test CI
  flags; worldv world 0/0 + model fixture 0/0 33 ids; ntwfmt 27 canonical; worldc world.bin identical to 4.1's; MinGW
  main.cpp 0/0 + worldmodel_test.cpp 0/0; census: 55,085 record lines identical, only 30 timing lines differ). Binaries
  newer than every source edit (incl. the yield/two-way-stop -> 0 control mapping). Extra after restart: ASan+UBSan+leaks
  run of worldmodel_test passed; wmcheck (scratch $S/wm/wmcheck.cpp, /tmp/city/wmcheck) on worldv's fixture world: 30
  files, 63 ids, 5 nodes 3 edges, 12/12 refs, no errors; doccheck same 2 known false hits.
- ~13:45 WORLD MODEL v0 SIGNED OFF to main (blobs only; /tmp/city/msg/wm_v0_signoff.txt; list /tmp/city/wm_blobs.txt, 23
  files; /tmp/city/apply_wm.sh <tree> [repo] guards bases: roads.cpp 120a1113 on c5bc0aab, roads.h 8f46021f on eee40e8e,
  ntwschema.h 29d50e6a on 862eac69, ntwschema.cpp 667e43c3 on 906d1d0f, NTW_FORMAT.md 1763c8dc on d318643d,
  world_format_test.cpp 24ff960c on 90f983c5, windows-build.yml 5d63b13a on afe28696; new: roadnet.cpp 610c4236,
  worldmodel.h bbaa9486, worldmodel.cpp 584aa248, worldmodel_roads.cpp f2230630, worldmodel_test.cpp 81f620d7 + 11
  fixtures). apply_wm.sh tested: reproduces $S/wm exactly on bcb8a71+fix+4.1; refuses on plain bcb8a71. SENT worldv the
  final API (/tmp/city/msg/wm_v0_worldv.txt). AWAITING the lead on: road fix (c5bc0aab), 4.1, v0.
- ~13:55 LEAD: road fix + 4.1 + world model v0 ALL ACCEPTED into SNAPSHOT 61 (HEAD + worldv slice 5 + AI set 19 + anim
  c12-c14 + night lights + my three; 46 files, tree 21d9af33; written from wm_blobs.txt/st41_blobs.txt with bases checked;
  worldv_test.cpp merged: my 4.1 hunk into slice 5's copy = 31bd856f; CI GCC steps running; if slice 5 checks fire on my
  samples the lead tells me+worldv). OPTIONAL procedural-only if cheap: (a) AI's I-38 median note: lanes 68/71 at the
  Bayshore Boulevard Exit gore leave 0.16-0.37 m to each lane edge; (b) plaza path 56305 through building 35588 (campus).
  NEXT = STAGE 1 in order: 1. interiors architecture note for the NIGHT agent (how hand-built interiors get lit);
  2. roads into world.bin + game reader in hand mode + lane graph reading authored lists (AI agent's API);
  3. Calle Luna pilot content. Heads-up: user's PC may become a worker (MSVC, GPU) for heavy builds later.
- ~14:10 NIGHT AGENT (a75cc482c6cf5fed6) AGREED the interior lighting plan (reply /tmp/night/msg/reply_interior_lighting.txt):
  R16_UINT room-id G-buffer target (MRT 6/8; 0 outdoor; index into the frame's room table; decals keep it; meshes per
  room/submesh; characters/vehicles/props per draw from nav room); tLightRoom u16 per light; tRooms[id] ambient+portals;
  tile pass collects <=4 room ids per tile; froxel fog: outdoor lights + camera's room only. Photometric table in C++
  keyed by catalog code= (lm, beam deg or point, K, emitter area or cd/m2, anim; derived cd, radius ~0.5 lux, cones):
  downlight 700 lm 3000 K 60 deg, troffer 3500 lm 4000 K 110 deg, pendant 800 lm 2700 K, table lamp 450 lm 2700 K, TV 250
  cd/m2. Window u32 record per window in one GPU buffer per building set; shell vertex carries window index (16 bits);
  keep shop-kind codes inside the 5-bit room type; time as integers (game day, minute) in the frame CB; night writes the
  agreement selftest. Caps 4096/256 hold; counters in --gfxstats for the pilot. Exposure: needs camera's room id each
  frame (cameraInInterior -> room id != 0). Additions: window flag frosted/obscured (bathrooms); daylight bounce
  constant per room; each fixture's room index + its floor's resident slot (drop a floor's lights at once); budgets
  G-buffer +2 B/px (4 MB 1080p), room table 64 B/room, window records 6-8 MB.
- ~14:15 LEAD: yes to all: RoadOverrides draft via the lead (AI owns LaneGraph::build; its stair pace cap first);
  worldc policy: world/district/signal/transit compile with roads; blocks/buildings/library stay errors (say so in the
  docs where worldc per-kind behaviour is described); roads order OK (section, shared reader, hand mode gRoads,
  round-trip test, then overrides); CAMPUS PATH: GO (own sign-off; census unchanged + AI --walkcheck); I-38: LEAVE (no
  barrier change); interior lighting: agree with night directly, then send HANDBUILT_WORLD 7.8 as a sign-off.
  SNAPSHOT 61 tree 21d9af33 is in the repo (HEAD still bcb8a71 while CI runs): base new trees on it ($S/s61).
- ~14:20 SENT: HANDBUILT_WORLD.md v1.2 sign-off (7.8 interior lighting; blob 415f44c9 on 6578a8ca; tree $S/doc78;
  /tmp/city/doc78_blob.txt) - lead noted it for SNAPSHOT 62 (after 61 merges). NIGHT confirmed 7.8 verbatim, waits for
  roomlight.h spec + test vectors to start the HLSL lit() + agreement selftest.
- ~14:25 CAMPUS PATH SIGNED OFF: src/world/campus.cpp 4e551482 on 3b31deca (tree $S/cp; /tmp/city/cp_blob.txt): walk to the
  field house (2046,3812, 44x18, front S) rerouted round its west end (2032,3850)->(2019,3838)->(2019,3797)->(2046,3797)->
  (2046,3803), paving pairs too. cq64: census 55,085 record lines identical; walkcheck through buildings 1 -> 0 (s61: link
  56194 through building 35568), carriageway 445/284 unchanged; MinGW 0/0.
- ROOMLIGHT (lit()) in progress: tree $S/rlt = s61 + src/world/roomlight.h + tests/world/worldmodel_test.cpp testRoomLight
  (104 vectors from $S/rl/rlvec.cpp, properties). 13 classes RS_ALWAYS..RS_PORCH (append-only, <=16), daylit = homes,
  garage, dusk, porch; intervals {start,end,chance(+1 of 256),level,jitter,daily}; roomLight(schedule, seed, windows,
  day, minute, night 0..255) -> 0..254. Scratch stats: $S/rl/rlstats.cpp. NEXT: build+run rlt test (CI flags), MinGW,
  sign off (roomlight.h new + worldmodel_test.cpp on 81f620d7), send night the spec + vectors.
- ROADS INTO WORLD.BIN in progress: tree $S/rb = s61 + roomlight (rlt's two files) + src/world/roadsbin.h/.cpp (sections
  "roads" v1 + "roads.ids"; writeRoads/readRoads; reader derives node edge lists, dist, length, hash) + handworld.cpp/h
  (hand mode reads roads when present) + main.cpp (+roadsbin.cpp in the unity build) + tools/worldc.cpp (modelKind:
  world/district/roads/signal/transit -> compileModel -> writeRoads + ids; other kinds error "no compiler ... yet") +
  build.sh WORLDC_SRC + docs (NTW_FORMAT section 1 bullet + 9.4 sentence + kerb bullet rewrap; HANDBUILT_WORLD 6.1
  "What it compiles so far" on v1.2) + worldmodel_test testRoadsSection (round trip exact, damaged sections refused).
  NEXT: build/run tests, worldc on world/ (world.bin identical) + on a temp world with roads, MinGW main.cpp, the game in
  hand mode with a roads world.bin (smoke: --world=hand --worldfile=...), then sign-off; then RoadOverrides draft via lead.
- ROADOVERRIDES DRAFT SENT via the lead: $S/ro/src/world/roadoverrides.h (host + MinGW syntax clean; message copy
  /tmp/city/msg/roadoverrides_draft.txt). LEAD: will pass it to the AI after its stair pace cap signs off and bring the
  answers back; "go on with the roads section in the meantime". Doc v1.2 and campus.cpp 4e551482 noted for SNAPSHOT 62.
- ~14:21 cq65 (/tmp/city/cq65.sh, status /tmp/city/rb.status): rlt test (CI flags) all passed, MinGW 0/0. rb: worldc on
  world/ identical to 4.1's; roads world "roads 9 nodes, 8 edges, 1 signalised"; block file rc 1 'no compiler'; rbcheck
  IDENTICAL; MinGW main.cpp 0/0 and worldc 0/0. BUT rb worldmodel_test 3 FAILURES: readRoads left half a network on
  truncated / extra byte / n0=1000. FIXED (14:28, before the exe build started 14:28:32): readRoads fills a local
  `RoadNetwork net` and does `out = std::move(net)` only on success (+ #include <utility>); old copy
  $S/rb_roadsbin_before_localnet.cpp. cq65 then goes on: nt_rb.exe build, hand mode on /tmp/city/rbroads_world.bin,
  hand mode embedded, procedural drive smoke -> cq65_done.marker.
- ~14:35 ROOMLIGHT: header comments changed (windows=1 also for exterior lights; "daylit" names dusk/porch). Doc 7.8 inputs
  now list the windows flag + darkness (night 0..255 a frame) and "facade evaluates lit() with day, minute and darkness":
  $S/rlt/docs/HANDBUILT_WORLD.md (on be73a5fb); rb's doc rebased the same (old $S/rb_doc_6ab36c6a.md). BLOBS
  (/tmp/city/rl_blobs.txt): roomlight.h a6fcf2b8 new; worldmodel_test.cpp cec03b2b on 81f620d7; HANDBUILT_WORLD.md
  c4f763a7 on be73a5fb (replaces be73a5fb for 62). Python port $S/rl/port.py (reads the packed table) matches all 104
  vectors; packed table /tmp/city/msg/roomlight_table.hlsl; HLSL sketch /tmp/city/msg/roomlight_hlsl_sketch.txt.
  SENT night the spec (/tmp/city/msg/roomlight_spec_to_night.txt; inputs: day=gameDay (starts 1; %7 0=Mon), minute from
  timeOfDay on CPU, night = renderer's call, suggested 255*saturate((4-sunElev)/10); porting notes; selftest sweep).
  SENT roomlight SIGN-OFF to main (rerun on a6fcf2b8 queued in cq66; tell the lead only if it fails).
- cq66 (/tmp/city/cq66.sh, pid 21877, status /tmp/city/rb2.status, markers cq66_rlt.marker / cq66_done.marker): waits
  for cq65_done, then rlt test + MinGW (a6fcf2b8), rlsweep (200k inputs, $S/rl/rlsweep.cpp) vs the Python port, rb test
  (readRoads fix), worldc again (world/ identical; roads world identical to cq65's; block rc 1), rbcheck, MinGW main.cpp +
  worldc. NEXT: when green + cq65's game runs OK -> roads blobs (bases: worldmodel_test.cpp on cec03b2b, HANDBUILT_WORLD on
  c4f763a7, NTW_FORMAT on 1763c8dc, main.cpp/handworld.cpp/.h/worldc.cpp/build.sh on 61's (git ls-tree 21d9af33),
  roadsbin.h/.cpp new; roomlight.h already signed) -> roads sign-off. Then RoadOverrides impl when the AI answers; then
  the Calle Luna pilot content.
- ~14:50 LEAD: roomlight a6fcf2b8 + worldmodel_test cec03b2b + HANDBUILT_WORLD c4f763a7 (replaces be73a5fb) NOTED FOR
  SNAPSHOT 62. Snapshot 61 = PR #54 (HEAD 1846b52 = 21d9af33 + PC_WORKER.md + tools/pc/next_package.sh), green, in Codex
  review. NIGHT replied: will write src/shaders/roomlight.hlsli + a renderer selftest (compute sweep + 104 vectors vs
  World::roomLight); Renderer::roomNight() = 255*saturate((4-sunElev)/10) raised by overcast/rain; Environment::gameDay
  (renderer.h); asked for roomMinute(float) -> ADDED in $S/rb/src/world/roomlight.h (goes with the roads sign-off, on
  a6fcf2b8) + test; told night. cq66 (rerun): rlt test + MinGW OK on a6fcf2b8; port vs C++ sweep 200k inputs 0 differ;
  rb test (readRoads local-net fix + roomMinute) all passed; worldc world/ identical to 4.1's; roads world identical;
  block rc 1; rbcheck IDENTICAL; MinGW main.cpp + worldc 0/0. NTW_FORMAT 9.4 paragraph rewrapped in rb.
- ~15:00 LEAD: PILOT PLAN APPROVED (/tmp/city/msg/pilot_plan.txt): part 1 streets after the roads sign-off; Q1 game.init
  in hand mode = MINE at the end of part 1, smallest app.cpp change as a guarded copy (lead asks the AI what traffic/peds/
  police assume about buildings/lots/sidewalks: the AI's files); Q2 night sets Environment::gameDay; Q3 procedural
  dressing OFF in hand mode; send the check map PNG when 1a is done -> SENT (/tmp/city/msg/pilot_streets_v1.png, _ids).
- PILOT 1a: authoring script $S/pilot/streets.py (grids $S/pilot/grids from /tmp/city/griddump; stable ids by position;
  writes roads.ntw, then run /tmp/city/ntwfmt on it), world files in $S/pilot/world (world.ntw, districts/cl/{district,
  roads}.ntw, lib/signals/cl_twophase.ntw). 52 nodes, 83 edges, 8.9 km, 9 signals, 7 all-way stops, 5 bulbs, Bougainvillea
  bridge (deck 5.5 m), one-way alleys, Palma jog, Calle Luna = class avenue 1/1 (priority), every other junction
  control=none. worldv 0/0, worldc OK. Check map: $S/pilot/checkmap.py <grids> <rbdump.txt> <census> <out> (rbdump =
  /tmp/city/rbdump world.bin, source $S/rb/wmtools/rbdump.cpp); orientation map $S/pilot/orient.py. Tools built:
  /tmp/city/{rbdump,ntwfmt,worldv}.
- PART 1 TREE $S/p1 = rb + pilot world files + 1c (cellgen.cpp: hand mode builds road cells with roads.dressing=false
  and returns before buildings; roadmesh.cpp: RoadCellOutput::dressing, dressCell only if dressing, roadWorkZoneAt false
  in hand mode) + PROFILE POINTS FIX in worldmodel_roads.cpp (polyline gets a point at each profile break; the bridge was
  flat before!) + testProfilePoints + 1b GRADING: worldmap.cpp's terrain block MOVED (mechanical, 83 lines) to
  src/world/terrainops.cpp (included by worldmap.cpp), roads.cpp's grading pass MOVED to src/world/roadgrade.cpp
  gradeTerrainForRoads(net, map, decks=nullptr) (+ deck skip, new) declared in roads.h; worldc: gBaked from
  compileTerrain, compileModel returns roads + decks, gradeTerrain() writes "terrain.grade" (i16 delta grid, height
  unit); handworld.cpp applies it before recomputeSplat; build.sh WORLDC_SRC += terrainops.cpp roadgrade.cpp; docs:
  HANDBUILT_WORLD 6.1 bullet, NTW_FORMAT section 1. testGrade in worldmodel_test. Backups: $S/p1_*_before.*.
  CHAIN cq68 (/tmp/city/cq68.sh, status /tmp/city/p1.status, markers cq68_tests/census/done): tests, worldc/worldv/
  ntwfmt/rbcheck on world/, MinGW x3, census rfcheck2 ($S/rf/rfcheck2.cpp = rfcheck + terrain hashes) rb vs p1, exe
  nt_p1.exe, hand-mode shots (aerial, luna street, bridge, heroes junction, night), procedural drive.
- ~15:35 ROADS SIGN-OFF SENT (/tmp/city/msg/roads_signoff.txt; blobs /tmp/city/rb_blobs.txt: roadsbin.h b349f1d7 new,
  roadsbin.cpp 250220cc new, handworld.cpp 6b769ce8/07589e1b, handworld.h 76052426/3ab407ad, main.cpp 392933be/96b7c6cd,
  worldc.cpp 4ef1bdf8/46563ddf, build.sh 597126f8/23675c45, NTW_FORMAT c1559061/1763c8dc, HANDBUILT_WORLD 2803a921/c4f763a7,
  worldmodel_test c9a77220/cec03b2b, roomlight.h 92d3b91c/a6fcf2b8). cq65 all green (hand mode roads world 9/8 read; embedded
  OK; procedural drive OK). LEAD: roads noted for SNAPSHOT 63; 62 CLOSED (source-built updater) and overlaps main.cpp
  (updater 5842de5d on 96b7c6cd) + build.sh (bbcfd32b on 23675c45) -> REBASED (clean merge-file): /tmp/city/rb62_blobs.txt
  main.cpp 5bde1642 on 5842de5d, build.sh ff1e292c on bbcfd32b (work in $S/rb62). Base PART 1 on 62 + roads set; part 1
  + roads together in 63 if ready. HEAD now c7a63ae (PR #54 merged; my files unchanged vs 21d9af33).
- cq68 RESULTS: p1 tests all passed (testProfilePoints, testGrade); worldc on pilot: roads 52/83/9, grade 5356 texels (3
  KB; /tmp/city/gradedump: 5303 lowered mostly <0.6 m, 53 raised = bridge abutments up to 3.71 m); worldv 0/0; ntwfmt OK;
  rbcheck IDENTICAL; MinGW x3 0/0; census rb vs p1 0/55086 differ + terrain hashes equal (moves mechanical); nt_p1.exe;
  hand mode pilot shots OK (/tmp/city/p1smoke/*.png: aerial, luna street, bridge, heroes junction, night dark). Lead saw
  them ("look right"; will send the user some with 63).
- LEAD APPROVED savegame.cpp guard (shared-file touch, base b1593762 = HEAD = shared; name it in the sign-off) -> DONE in
  p1: saveGame/loadGame return false in hand mode (+ include ../world/handworld.h). app.cpp (base 30f8a386) in p1: game.init
  when hand world has roads; hand mode never opens the menu; --play/--autoplay start a game, else free camera.
  CHAIN cq69 (/tmp/city/cq69.sh, status /tmp/city/p1b.status): after cq68 done: MinGW, nt_p1b.exe, hand shots with game
  initialised, hand --play --autoplay drive (40 s), procedural drive. NEXT: when 62 commits: build $S/p62 = 62 tree + roads
  set (rb62 merges) + part 1 changes (merge build.sh: p1's WORLDC_SRC line on bbcfd32b) -> rerun checks -> part 1 sign-off.
- ~16:21 SNAPSHOT 62 COMMITTED: HEAD 7b9146d (tree ce441e47) = updater + campus 4e551482 + doc c4f763a7 + roomlight
  a6fcf2b8 + worldmodel_test cec03b2b + PC worker; main.cpp 5842de5d, build.sh bbcfd32b, build.bat 0aa4c091, CI yml 93c8045f.
  $S/s62 = archive of 7b9146d (+ s61's shaders_embedded.h; no shader changes 61->62). $S/p62 = s62 + roads set (rb files,
  rb62 merged main.cpp/build.sh) + part 1 (p1 files; build.sh 3-way merged) + world/ pilot + wmtools.
- cq69 (p1 + app.cpp/savegame): MinGW 0/0; nt_p1b.exe; hand shots with the game initialised OK (Lane graph: 192 lanes,
  397 connectors, 330 walk nodes, 443 walk links; Story places resolved: diner 1319 330, Mari's 1720 362, boatyard 1617
  235; 57 missions); HAND AUTOPLAY DRIVE OK (spawn on the Paseo del Rio 1764,220; 76-89 peds, 54-67 vehicles; 0 fatal;
  shots /tmp/city/p1bsmoke/auto_drive_0*.png show traffic, peds, minimap). Bridge side shot good.
- CHAIN cq70 (/tmp/city/cq70.sh, status /tmp/city/p62.status, markers cq70_tests/done): on p62 after cq69: CI's six GCC
  steps, worldc/rbcheck, MinGW, census s62 vs p62, nt_p62.exe, hand shots, hand autoplay drive, procedural drive. THEN:
  /tmp/city/mk_p1_blobs.sh -> /tmp/city/p1_blobs.txt -> PART 1 SIGN-OFF (name app.cpp + savegame.cpp as shared-file
  touches; bases 62's / the roads set's). Part 2 plan draft: /tmp/city/msg/part2_plan_draft.txt.
- ~16:40 LEAD: PART 2 APPROVED (2a now; 2b section layouts forwarded to the AI with RoadOverrides after its stair cap; 2c
  ~25 pilot blocks; beyond the pilot waits for the user). Snapshot 62 on main (0bdeab3, PR #55). Part 1 -> snapshot 63
  with the roads set once green on 62 + roads. Lead sent the user the auto_drive + bridge shots.
- PART 2a IN $S/p2 (= p62 copy, no build outputs) - written, NOT yet compiled (one build at a time: waits for cq70):
  worldmodel.h: GroundPaint/Pad/Fence, Parcel graded/grade/blend/ground/paints/pads/fences, Plaza, Dress (DRESS_TREE/
  PLANT/PROP/LIGHT, hasZ/hasYaw...), Block.plazas/dressing, LibItem code/size/variants/collide, Model.plazas/dressing,
  BlockPad, BlocksOut, compileBlocks decl (+ #include blocksbin.h); worldmodel.cpp loader typing; NEW
  src/world/blocksbin.h/.cpp (HandBlocks ground/props/lamps/lampSched + byCell index; "blocks" section v1; gHandBlocks),
  src/world/catalogcodes.h (propTypeByName, propYawOffset, LampSpec table: PROP_STREETLIGHT, LIGHT_PORCH,
  LIGHT_WALL_FLOOD, LIGHT_GARDEN), src/world/worldmodel_blocks.cpp (compileBlocks); terrainops.cpp flattenPoly (+
  worldmap.h decl); roadnet.cpp roadSurfaceHeight (split from roads.cpp surfaceHeight, mechanical) + roads.h decl;
  worldc.cpp (block+catalog model kinds, compileBlocks, placeOnTerrain: pads then roads grading, z fill, writeBlocks);
  handworld.cpp/h (readBlocks -> static HandBlocks, gHandBlocks; log line now "Hand world: terrain, N road nodes, ...,
  ground polygons, props, lamps"); cellgen.cpp buildHandBlocksCell (ear clipping, draped/flat ground meshes, props,
  lamps); main.cpp += blocksbin.cpp; build.sh WORLDC_SRC += blocksbin/worldmodel_blocks/catalogcodes/worldtypes; docs
  NTW_FORMAT (section 1 + "What the blocks compiler reads"), HANDBUILT_WORLD 6.1; tests: samples typing, testBlocks.
  CONTENT: world/lib/catalog.ntw (22 pieces), world/districts/cl/blocks/b010/block.ntw (Domino Park). worldv 0/0.
  CHAIN cq71 (/tmp/city/cq71.sh, status /tmp/city/p2.status) after cq70: CI six, worldc (p62 world identical), rbcheck,
  MinGW x3, census s62 vs p2, nt_p2.exe, Domino Park shots day/night, hand drive, procedural drive.
