# Tools engineer (worldv, the world validator) resume notes
## Role (from lead, 2026-10-20 ~01:55)
- Own: tools/worldv.cpp, src/world/validate*.h/.cpp, tests/world/worldv_test.cpp + fixtures, CI steps (one yml edit).
- City agent owns ntw.h/.cpp, ntwschema.h/.cpp, gridcodec, lz, worldbin, handworld, worldc, ntwfmt, terrainbake, docs.
  ntw.h/ntwschema.h frozen at v1 (additive only, via city agent). worldmodel.* will be city's; until then read
  Ntw::Document through small accessors; model needs -> API requests to city agent.
- Design: docs/HANDBUILT_WORLD.md v1.1 = blob 6578a8ca (sections 9.1, 4.9, 12.6). Format: s2c/docs/NTW_FORMAT.md.
- Slices: 1 framework (discovery like worldc, parse+schema+canonical check, diagnostics file:line sev id msg, exit 1 on
  errors, summary, --only, incremental by content hash <5 s) + 2 world-level (ids, path==id per NTW_FORMAT s5, refs kind)
  -> sign off; 3 layout library checks (4.9 stairs/doors, 9.1 walls/rooms/doors/reachability) -> sign off;
  4 CI yml blob (updater test + city's world_format_test line + worldv on world/) -> sign off.
- Rules: host-only, g++ -Wall -Wextra -Werror + UBSan, clang MSVC-safety, snapshot-safe blobs, ask city agent directly.
## Paths
- private tree /tmp/tools/tree (from s2c); samples: $SP/s0test/samples (16 files, must stay clean)
## Log
- ~02:10 read worldc/ntwfmt/ntw.cpp/ntwschema.cpp/world_format_test + 16 samples. HEAD shared = 82b9e60 (snap 58);
  59 (step 2 + updater) not committed yet. City agent = SendMessage to "aa6f7b18ee2c9d06f".
- SENT city: ask 1 CI line for world_format_test (+ timeout 5->10?), ask 2 RefDef table (struct RefDef {kind,type,key,
  pattern}; schemaRefs(); classes district/node/edge/busstop/block/parcel/lot/building/line/plan/layout/unit/fitout/set/
  facade/signal/palette/kit/prop/veg/light/fence, .elem local, f.elem floor-qualified, floor), confirm id scopes,
  patterns, paths, samples = clean in file mode. Prototype the table in MY tree only until the city's blob arrives.
- 02:19 CONTAINER RESTART (all processes killed). 02:22 checked: /tmp/tools/tree intact (validate.h complete,
  ntwschema.h/.cpp = city's + PROTOTYPE RefDef (diff /tmp/tools/ntwschema_proto.diff; city originals
  /tmp/tools/ntwschema.{h,cpp}.city, blobs h 4336962a cpp 26887228), compile ok). No build outputs. HEAD f595703
  (snap 58 merged); 59 re-running smoke. City agent restarting from notes: resent my asks (may have been dropped).
- Design decided: validate.h (Diagnostic{file,line,severity,check,message}, Options{world,files,only,cache,incremental,
  jobs}, Summary, run(), checkText(), formatDiagnostic/Summary). Check ids: file.read/parse/schema/canonical, id.form,
  id.unique, id.path, ref.form, ref.missing, ref.kind. Cache build/worldv.cache (text; per file: diags + defs + scoped
  + refs; keyed rel+FNV64 of raw bytes; header rules hash = kRulesVersion + schema + refs). File runs (positional
  args) = per-file checks only (samples must be clean there); world runs add path/unique/cross-file refs.
  Fixtures: tests/world/fixtures/samples (16 exact), fixtures/world (samples at real paths + completions/stubs),
  bad/ with "# expect <check>" comments; world-level bad variants as inline mutations in the test.
- ~02:55 city: took RefDef table (+ override remove, layout spot for=<.door|.counter>, lift door= now a word), dropped
  f.sign; id rules in NTW_FORMAT s5 (numbered >= N digits; zone/anchor <d>.<name>; plaza/lot/spot <block>.<name>;
  transit line <kind>.<name>; element misses = WARNINGS). CI step name "Test world file formats (NTG grids, NTW parser,
  formatter and schema, LZ, world.bin)", timeout 10 OK. Updater committed d208e02 (snap 59, PR #52).
- REBASED: /tmp/tools/tree = city's s2d (HEAD d208e02 + step 3 + step 4) + my files (old tree: /tmp/tools/tree_s2c).
  worldv_test all passed; samples clean (file run), fixture world 30 files clean, world/ clean.
- GATE: /tmp/tools/gate.sh MB (lead flag + memfree). One compile at ~02:55 ran at 2356 MB free (< 2500): gate EVERY
  compile from now on: sh /tmp/tools/gate.sh 2500 && g++ ...
- ~03:00 SLICES 1+2 DONE, blobs written (72 new files, none at HEAD): manifest $SP/tools/slice12/manifest.txt,
  apply.sh <tree>, verify.sh <tree> (tested on a copy of s2d: all ok). validate.h 395f6953, validate.cpp a2da4747,
  worldv.cpp 36c9405f, worldv_test.cpp fc7b0a06. Bench (synthetic 37,304 files / 24 MB, $SP not used: /tmp/tools/bench):
  full 3.1 s (4 jobs), incremental unchanged 1.3-1.8 s, 3 edits 1.5 s, peak RSS 222 MB. Builds under load 15:
  worldv 10 s, test 22 s, test run 2.2 s. clang -Wshadow-all/-Wconversion 0, MinGW -Werror ok (worldv + test).
  Lead: updater merged 837b39c (PR #52), MSVC CI green; city steps 3+4 = snapshot 60 (starts ~30-60 min after 02:55);
  rebase worldv on HEAD after 60 commits (lead tells me); CI blob base = HEAD's windows-build.yml.
- NEXT: send sign-off to main; then slice 3 (4.9 stairs/doors, 9.1 walls cross/rooms closed/doors fit+join/reach).
- ~03:05 LEAD: slices 1+2 ACCEPTED, folded into snapshot 60 (89 files with city steps 3+4). Include worldv_test in the
  slice 4 CI blob. Go on with slice 3.
- SENT city slice-3 geometry questions (A doors/windows at=centre?, fits, joins rooms; B stair run per kind, tread =
  flight length/treads, landing depth, walk=, radius; sample st2 spiral = 0.247 m tread -> ERROR?; C door landing 0.9 /
  turn-behind 1.2 m; D corridor (hall too?) width, dead-end landings; E walls cross, rooms closed/overlap, reachability,
  attic "no way in" warning vs plan access=). Samples layout.ntw/layout_core.ntw are excerpts -> fail rooms-closed (and
  core d2 door landing 0.5 m) in file runs: needs a decision (city completes them, or tell lead).
- Slice 3 plan: src/world/validate_layout.h/.cpp (checkLayout(doc, diags)), called from checkFile for layout/unit/fitout
  (deep, valid). Check ids: wall.cross room.closed room.overlap room.reach door.wall door.rooms window.wall window.room
  door.width door.landing corridor.width stair.tread stair.riser stair.landing stair.walkline stair.width stair.radius
  stair.busy.
- ~03:10 city ANSWERED slice-3 semantics (at = centre; margins t/2+0.05; out only via ext= (units: outside slot rect on
  a boundary wall); .slot in leads after snap 60 (city follow-up 4.1); run = flight CENTRELINE (dogleg p1/p2 at landing
  mid-depth, flight = seg - landing/2; quarter p1 = landing centre; winder p1 = turn; spiral/curved arc fitted, walk from
  centre default R); tread = walking-line length / treads; landing default w; door landing: flight-end nosing lines in
  front within door w +-0.3 m >= 0.9 m from wall face; turn warning w<0.9 & free depth <1.2; corridor AND hall < 1.0;
  outdoor rooms exempt from closure; noway warning at layout level, plan pass counts access=hatch).
  City step 4.1 (after snap 60): completes samples layout.ntw + layout_core.ntw (my st1 geometry, st2 treads=12
  rise=0.22) -> until then my test EXPECTS the 6 sample findings.
- Slice 3 code: src/world/validate_layout.h/.cpp (checkLayout), hooked in checkFile (deep, valid, layout kinds);
  noway: world runs -> scoped fact "<layout>/#noway", plan floors with access= -> "<plan>/<floor>#access", world pass
  wayIn() suppresses when hatch or a layout-scoped stair/lift ref reaches the floor (reached set). 19 new check ids.
  Bad fixtures: layout_walls/rooms/doors/stairs/reach/noway/corridor, unit_doors (+ ^ marks = record's first line).
- ~03:30 SLICE 3 DONE (pending CI sim): checks all pass (UBSan test, clang 0, -Wconversion 0, MinGW ok); bench with
  slice 3: full 5.2 s, incremental 1.9 s, after a layout edit 2.75 s. Blobs: $SP/tools/slice3/{manifest,changed}.txt
  (82 files, 19 changed/new vs slice12: validate.cpp cb363980, validate_layout.h 4a275030, validate_layout.cpp
  3b9c0a1f, worldv.cpp 1cf4b975, worldv_test.cpp 1bf609fa, ...), apply.sh/verify.sh. Applied onto s2d+slice12 copy =
  /tmp/tools/s60_3 (== my tree).
- SLICE 4 draft: /tmp/tools/ci/windows-build.yml (base HEAD 837b39c blob 51576939): timeout 5->10, + 4 steps (updater
  test, world formats, worldv_test, worldv on world/ --no-cache). CI sim: sh /tmp/tools/ci/run_gcc_job.sh <tree>.
- ~03:40 SENT slice 3 sign-off (manifest $SP/tools/slice3, 19 changed/new) and slice 4 sign-off (CI blob afe28696 on
  HEAD 837b39c's 51576939; copy $SP/tools/slice4/windows-build.yml). CI sim (run_gcc_job.sh on /tmp/tools/s60_3):
  5 steps pass (0.2/4.1/8.0/22.6/13.6 s).
- PENDING: lead's acceptance of 3 and 4; snapshot 60 commit -> rebase check (verify.sh on HEAD); city's step 4.1
  (samples layout.ntw/layout_core.ntw completed + .slot in leads row) -> my follow-up blob: refresh
  tests/world/fixtures/samples copies, testSamples expects 0, add a slot-entry door fixture (leads=corridor:u1).
- ~03:50 fuzz: ASan+UBSan worldv (/tmp/tools/bin/worldv_san) on 150 randomly damaged copies of the fixture world (/tmp/tools/fuzz.py): 0 crashes/sanitizer errors. Monitor on HEAD (snapshot 60) armed.
- ~03:30 LEAD: slices 3+4 ACCEPTED, in snapshot 60 (100 files); PR #53's CI runs my 4 steps (lead reports failures).
  NEXT: (1) refresh blob after city's 4.1 (samples copies + expect clean + slot-entry fixture); (2) plan stage-1 road
  and AI checks (12.6) with the world engineer against its worldmodel API.
- SENT city the stage-1 plan: worldv natively builds RoadNetwork (city's compileRoads) + LaneGraph::build (AI's
  lanes.cpp, like traffic_sim) and only checks. API asks A loader w/ file:line, B compileRoads + index->id maps,
  C blocks/buildings in world frame (footprints, entrances, colliders, fences, spots), D typed signal plans, E transit/
  places, F LaneGraph outputs (lanes kerb, connectors conflicts/yield, boxes, plans, walk graph, counts). Order: 1 plan
  pass now (Ntw only: stairs/lifts floors, hatch, sill/door h) -> 2 A+B+D+E (road.overlap/moves/sector, signal.cycle,
  parking.clash, transit stops) -> 3 F (road.lanes/connected, signal.cover/conflict, busstop.kerb, summary) -> 4 C
  (placement, walk.links, spot.place, dressing, walk.level). Asked: worldmodel v0 timing/shape; who calls LaneGraph;
  OK to start the plan pass now.
- ~03:20 LEAD: go ahead with SLICE 5 = plan pass (stairs/lifts vs floors at floor heights, hatch lands in a room,
  window h+sill / door h vs floor h, door swings vs fixed furniture if data allows); same rules (expect-mark fixtures,
  planted errors, CI flags, synthetic timing, blob list). Road/AI steps 2-4 wait for worldmodel; NO own model.
- City answered slice-5 Qs: Q1 treads = riser count, treads x rise = dz +-0.05, derive missing one (whole count), none
  -> round(dz/0.175); Q2 check vs the floors named (relative form from=<room>, to=+1.<room>/-1.<room> comes in 4.1);
  Q3 ends in from/to rooms, to above from; Q4 lifts same id/at/size within 2 cm on every stop floor, differing stops=
  is an ERROR; Q5 hatch error if no room; Q6 h = floor-to-floor, no slab; Q7 door swings NOT settled -> defer.
- Slice 5 code written: validate_layout.h PlanFloor/PlanFinding/checkPlanFloors; validate_layout.cpp PlanPass
  (stairs, lifts, hatch, heights; Item.out now a pointer; treadRules() shared); validate.cpp planPass() in the world
  pass (parses plan + layout docs from file texts, dedupes repeated-layout findings); 5 new check ids (stair.floors,
  lift.stops, floor.access, door.height, window.height). Fixture world: core_a st2 -> to=f1.flat rise=0.24; walkup_a
  roof access=hatch:f3.hall. Test: 9 new plants. 03:37 RESTART killed the test build -> rerun.
- AI agent's LaneGraph answer: $SP/tools/lanegraph_answer.txt (stage 1; 5 gaps to derive myself: kerb flag, box
  polygon, explicit movement/walk lists per phase, sidewalk kerb/building sides, road indices for corner/plaza/
  site-zebra links).
- ~03:55 slice 5: worldv_test ALL PASSED (plan pass incl. grouped lift findings, fixtures/plans world with marks, 9 plants). Running check suite + bench (bknf9anjz). Blob scripts in $SP/tools/slice5.
- ~04:05 slice 5 checks: worldv_test all passed; worldv -Werror; world/ + fixture world 0; plans fixture 8 expected;
  clang 0 (shadow-all, conversion); MinGW ok. Bench under load 11: plan pass CPU cost ~0.1-0.15 s on 37k files
  (interleaved user time 3.04/3.16 s slice-3 binary vs 3.19/3.25 s slice-5, --jobs=1). Blobs: $SP/tools/slice5
  (86 files, 10 changed/new: validate.cpp ac145311, validate_layout.h b8d0a77a, validate_layout.cpp b4ed0d75,
  worldv_test.cpp acade656, 4 fixtures/plans files, world core_a 99cb67f0, walkup_a 05418969). Applied on s60_3 ->
  /tmp/tools/s60_5; CI sim running (b1mq0e8cn).
- ~04:10 SENT slice 5 sign-off (CI sim 5/5 at load 11: 0.5/9.8/19.3/49.6/26.4 s). Pending: lead's acceptance; snapshot 60 commit (verify on HEAD); city 4.1 (samples, .slot leads, relative stair floors -> my resolver + plan pass + refresh blob); stage 1 waits on worldmodel.
- ~04:15 LEAD: slice 5 ACCEPTED (goes into snapshot 61; 60 is building). PARKED. Queue, both waiting on others:
  (1) after snapshot 60 commits (lead tells me): sh $SP/tools/slice5/verify.sh on a HEAD checkout (git archive HEAD)
      + apply slice5 if not yet in HEAD; re-sign only if something changed.
  (2) after the city's 4.1 lands (city tells me): one blob = refresh tests/world/fixtures/samples/{layout,layout_core}.ntw
      from the city's samples + testSamples expects 0 findings; a slot-entry door fixture (leads=corridor:u1, needs the
      .slot leads row); relative stair floors (from=<room> this floor, to=+1.<room>/-1.<room>) in the Scanner's
      floor-ref scoping, the world pass's layout-scoped resolution, and PlanPass::stairs (resolve relative to each
      using floor). Base on HEAD then.
  Stage 1 (road/AI checks) waits on the city's worldmodel v0 header draft; AI LaneGraph answer in
  $SP/tools/lanegraph_answer.txt (5 gaps to derive). No own model.
- ~04:45 snapshot 60 MERGED bcb8a71 (PR #53), GCC CI green on GitHub. HEAD has exactly my slice 1-4 blobs (82/82) + workflow afe28696; city API files == s2d. /tmp/tools/head60 = git archive bcb8a71 + slice5 apply (== my tree). First verify run was UNGATED (2274 MB free) -> killed by PID, verify.sh now gates each compile and treats the samples run as informational until 4.1. Rerunning verify + CI sim (bw3ser0ix).
- ~04:55 VERIFIED on HEAD bcb8a71 + slice 5: verify.sh all ok (samples' 6 excerpt findings as expected), CI sim 5/5 green (0.4/8.2/17.9/53.8/23.3 s at load). Slice 5 blobs unchanged (their bases = HEAD's files) -> no re-sign. PARKED until the city's 4.1.
- 05:18 NEW TASK (USER via lead), FIRST: updater builds from source (main branch / merged PRs) instead of release
  exes, and shows new PRs. worldv follow-ups wait. Notes for this task: $SP/resume/autoupdater.md (updater section
  "SOURCE UPDATES"). Container restarted ~05:15.
- ~07:40 city 4.1 done: relative floors `from=0:stair to=+1:stair|-1:stair` (':' not '.'), door leads .slot, samples
  whole; its worldv_test hunk 99719d70 (want={}) OK'd to city + lead. Worldmodel v0 header draft reviewed
  ($SP/wm/src/world/worldmodel.h): asked discover()+loadParsed(), Error code, Node::edges/junction, stable order,
  read-only model; questions: terrain nullptr in compileRoads, shape includes node points. Still on updater task.
- ~13:40 city: worldmodel v0 WRITTEN + tested in $SP/wm (worldmodel.h/.cpp, worldmodel_roads.cpp, roadnet.cpp,
  tests/world/worldmodel_test.cpp), signed off to lead (lands after the road fix + 4.1). All 10 review points in:
  ntwschema parseRefPattern/matchRefs/refRules (my matcher moved over; use them instead of my copy), Ref{text,
  classes,cls,target,src}; discover(dir,errors) + loadParsed(vector<File>&&,...) (File::valid); Error{src,code,msg}
  codes file.*, ref.form, value.form, road.node/loop/shape/profile; Node::edges/junction; stable order; read-only.
  compileRoads(model, RoadNetwork&, RoadIndex&, errors): NO terrain (absolute z). Shape includes node points (2 cm).
  Link: loader = ntw.cpp+ntwschema.cpp+worldmodel.cpp; roads add worldmodel_roads.cpp+roadnet.cpp+core/math.cpp;
  LaneGraph::build needs traffic_sim's include list. Compile rules in NTW_FORMAT 9.4 (control mapping 0/1/2 etc.).
  -> stage-1 step 2 starts on this after the updater sign-off.
- ~14:55 URGENT worldv fix for PR #54 (lead): relative stair ends 0:/+1:/-1: in the plan pass (validate_layout.cpp
  stairs()/stairEnd()/neighbour(), room on the reached floor, reached floors -> validate.cpp wayIn after planPass),
  fixtures plans/ tower_b (clean) / tower_c (+1 off top) / tower_d (not above), worldv_test testRelativeStairs (+5
  plants), testPlanPass 13 files. All passed CI flags + ASan. SIGNED OFF on 1846b52 (blobs /tmp/wv54/blobs.txt,
  tree /tmp/wv54/tree). Back to updater.
- ~17:55 PARKED (usage). Updater: PC live test 2026-10-20-03 PASSED; nothing more owed (optional low-priority: latch
  short key taps in win32.cpp's window proc until the next frame reads them).
- worldv STAGE-1 SLICE 6 (lead approved the plan; city agreed every finding): private tree /tmp/rc6/tree (base
  /tmp/rc6/base = git archive 0bdeab3). New src/world/validate_roads.{h,cpp}; validate.cpp (ntwschema matcher +
  discover(), per-file model checks in checkFile, WorldChecks::roads(), 16 new check ids, rules 3), validate.h
  (Summary::roads), tools/worldv.cpp (model includes, roads line), worldv_test (Ntw pattern tests, testRoads
  plants), world fixture roads.ntw extended (grid, 4-leg signal at n0012, bus.8 = e0011,e0007), bad/roads_model,
  signal_plan, signal_empty, district_values. Base blobs on 0bdeab3: validate.cpp 8ae2c086, validate.h 395f6953,
  worldv.cpp 1cf4b975, worldv_test 99155a63, fixture roads.ntw db9173eb, bus.8.ntw ac8aaa72.
  STATE: worldv built (/tmp/rc6/worldv); test passed UBSan+ASan BEFORE the last 2 plants; now 1 FAIL: plant
  "approaches= with a letter that isn't one, and a letter twice" -> extra signal.node because legLetters() uses
  every approaches pair. FIX: legLetters must skip bad letters and a letter's 2nd use (same as sectors()), rebuild,
  rerun test (UBSan+ASan, CI flags), then blobs (git hash-object -w) + sign-off to lead.
  Pilot (city's $SP/p62/world and $SP/p2/world): 0 errors, 4 signal.node WARNINGS expected on 63's world/
  (cl.n0003/n0004/n0006 T's until stage 2's west legs, cl.n0039 a real T; 4-way cl_twophase) - settled with city.
  City is fixing: samples bus.8 route -> e0011,e0007; model fixture cl.e0003 moves -> l,r; NTW_FORMAT 9.12 route
  wording. Slice 7 next: LaneGraph (lanes nowhere, signal cover/conflict, walk checks).
