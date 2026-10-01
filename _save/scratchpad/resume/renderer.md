# Renderer agent: resume notes (written 2026-10-01 ~21:10 UTC, wrap-up)

Rules that still apply: C++17 + HLSL only, no third-party code or external assets, no placeholders/TODOs,
unity build (file-local helpers in NAMED namespaces, never `near`/`far`), never commit/push the main repo (the lead
integrates). Builds only through `build.sh`, games only through `tools/run.sh`, one build and one Wine game of mine at
a time, `nice -n 10`, Wine prefix `/tmp/wine_fx`, Wine slot 4 is mine (`NT_D3D12_SLOT=1`), memory gate stays on
(runs may die with rc 137 under the cap: just re-run). Before any host-g++ test compile: `sh tools/memfree.sh` >= 1500.
Sign-off message to the lead: "renderer snapshot-safe" + file list with `git hash-object -w` blobs.

## Merged / in flight
- Snapshot 22 (merged, main 4b6357f): character shading (skin LUT, eyes SM_EYE + refraction, hair KK, feature
  shadows, eye-socket light cut-off, `--shaderdir`), plus the faces agent's shader-pupil flip.
- Snapshot 23 (merged, 6780edd): city agent (pavedAt / pavedGrid, back lots). Not mine.
- Snapshot 24 (verifying, tree aaa0ff8 = 6780edd + my 7 phase-1 blobs + AI set 2): vegetation PHASE 1, GPU-driven
  props. Blobs: props_render.cpp 2767a795, renderer.cpp 37ebab81, shadows.cpp ab119fdb, propcull.hlsl 44798739 (new),
  gfx_tools.cpp 1aa4c153, gfxtest.hlsl d6f41f61, PROGRESS.md ed6a9f8a. Lead's copy: scratchpad/renderer_p1_blobs.txt.
  Proof: self-test 19 PASS + 1 SKIP (20th check "GPU-written instances, indexed indirect"), shc 146/0, image diff
  base vs phase 1 at tour stops 4-13 (/tmp/fx/diff_p1_sheet1.png, diff_p1_sheet2.png), gfxstats draws 525 -> 468.

## Vegetation plan (approved by the lead: three phases, each its own sign-off)
1. GPU-driven props: DONE (snapshot 24).
2. Decor plants placed on the GPU (crotons, hibiscus/ixora along walls/fences/hedges and in planting beds, ferns and
   saw palmettos on the forest floor, flower clumps in lawns/meadows). CODE COMPLETE, NOT YET VERIFIED.
   (LODs / impostors dropped for now: prop meshes are ~500 verts and phase 1 removed the CPU cost; revisit only if
   phase 3's denser palms show in the gbuffer timer.)
3. Palms (feathery twisting fronds, leaf-scar trunk rings in the shader, round coconuts) + leaf-canopy material for
   hedges / garden shrub masses (MAT_LEAVES). CODE COMPLETE, BUILT, NOT YET RUN.
   Lead allowed: edit ONLY the vegetation builders in src/world/propmesh.cpp (palm, palm_tall, oak, bush, street tree,
   hedgeMesh); keep footprint, collision proxy and placement anchor unchanged. GPU-placed cover must stay off roads,
   sidewalks, driveways, parking, building footprints, and be deterministic per cell.

## Private trees (git repos under /tmp/fx, local commits only)
- /tmp/fx/gd   : phase 1 as signed off (commit 50dba6d), base 4b6357f. Identical to the snapshot-24 blobs.
- /tmp/fx/gd2  : phase 2 on 6780edd + phase 1 (commit 050d7b3). Phase-2 files vs phase 1:
    src/render/vegdecor.cpp (NEW: decor prototypes, namespace Render::vegdecor),
    src/render/props_render.cpp (decor protos after the world ones, DecorInputs, decor buffers, segSize per proto,
      slotCapacity rename, csDecorPlace dispatch + combined cull + args reset),
    src/render/renderer.cpp (#include "vegdecor.cpp"; DecorInputs filled from terrain splats + weather overhead),
    src/shaders/propcull.hlsl (csDecorPlace, decor range in csPropCull, segSize checks, decor count reset),
    src/shaders/overhead.hlsl (overhead class: 1 lawn, 0.25 MAT_DIRT facing up = planting bed, 0 else),
    src/shaders/grass.hlsl (comment only). PROGRESS.md still needs a phase-2 paragraph.
- /tmp/fx/gd3  : phase 3 on top of gd2 (commit f46f6d9). Phase-3 files vs gd2:
    src/world/propmesh.cpp (new helper palmFrondStrip; palm builder: trunk makeMat(MAT_BARK, 2 coconut / 3 royal),
      10-sided trunk without the old radius zigzag, palm fronds via palmFrondStrip, sphere coconuts; shared `frond`
      helper left unchanged on purpose),
    src/shaders/props.hlsl (MAT_BARK param 2/3: leaf-scar ring shading along uv.y/3),
    src/shaders/matgen.hlsl (genLeafCanopy, GEN 28), src/render/materials.cpp (MAT_LEAVES -> GEN 28).
- Older trees (character shading, done): /tmp/fx/cs2 (shading), /tmp/fx/cs2f (+faces batch 1), shader folders
  /tmp/fx/shd_v4 (final shading shaders), /tmp/fx/shd_off.
- Base build copies (safe to delete): /tmp/fx/gd0, gd1b, gd1s, gd1m, gd2b, gd3b, cs2b, cs4b, cs4fb.

## Exes (in /home/user/GTA-6-Claude-v0.5/bin, QUICK -O1; delete when done)
- nt_gd0.exe  base (46800db + shading), the "before" for phase 1/2 diffs
- nt_gd1.exe  phase 1 on the same base (the "before" for phase 2)
- nt_gd1m.exe phase 1 on 4b6357f (self-test passed with it)   - nt_gd1s.exe superseded, delete
- nt_gd2.exe  phase 2 (built on the OLD base 46800db + shading + p1; fine for A/B vs nt_gd1)
- nt_gd3.exe  phase 3 (= phase 2 + phase 3, old base)
For the final sign-offs rebuild from /tmp/fx/gd2 and /tmp/fx/gd3 (now on 6780edd).

## Evidence and logs (/tmp/fx)
- Phase 1: log_gd0a.txt, log_gd1a.txt (stops 4-13, synctimers + gfxstats), gd0a_/gd1a_auto_tour_NN_*.png,
  diff_gd0a_gd1a_NN.png, diff_p1_sheet1.png, diff_p1_sheet2.png, log_gd1m_selftest.txt.
- Phase 2: log_gd2a.txt + gd2a_auto_tour_04..10 (run killed rc 137 after stop 10; urban stops, no decor in range),
  diff_gd1a_gd2a_NN.png; 1080p partial: gd2h_auto_tour_11_key_coral_marina.png, gd2h_auto_tour_12_sawgrass_dawn.png.
- Character shading proof: /tmp/fx/fc/proof2_M0_G_{day,night}.png, proof3_1m_{day,night}.png, proof3_4m_night.png,
  goggles_4m_night_M0_G_F44.png, F4_eyes_1m_day_zoom.png.

## Scripts
- /tmp/fx/tour_multi.sh   EXE=bin/nt_X.exe P=tag START=n COUNT=k [W=1920 H=1080] EXTRA="--synctimers --gfxstats"
                          (slot 4; shots -> /tmp/fx/tag_auto_tour_NN_name.png, log -> /tmp/fx/log_tag.txt)
- /tmp/fx/imgdiff.sh A B  per-stop RMSE + pixels > 8/255 + diff sheets diff_A_B_NN.png
- /tmp/fx/perstop.py logA logB   per-stop synctimers (shadows, gbuffer, lighting, total) + draws/indirect/dynKB
- /tmp/fx/passtimes.py, /tmp/fx/gfxstats.py   run means
- /tmp/fx/proofsheet2.sh A B [OUT], proofsheet3.sh OUT DIST TAGS..., facecam_run.sh, facecam_shots.py (faces)
- /tmp/fx/integrate_p1.sh (pattern for guarded copies into main; adapt for phase 2/3: refuse files changed in main)
- shader check: sh /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad/shc_d3d12/shc_d3d12.sh TREE /tmp/wine_fx
  (gd2: 147 shaders 0 failed; gd3: 148 shaders 0 failed)

## Next steps
1. Phase 2 verification (tree /tmp/fx/gd2):
   - Rebuild on the new base: `rm -rf /tmp/fx/gd2b && mkdir /tmp/fx/gd2b && (cd /tmp/fx/gd2 && tar --exclude=./build
     --exclude=./bin --exclude=./.git -cf - .) | (cd /tmp/fx/gd2b && tar xf -) && cd /tmp/fx/gd2b && QUICK=1
     OUT=/home/user/GTA-6-Claude-v0.5/bin/nt_gd2.exe nice -n 10 sh build.sh` (and the matching "before": phase 1 on
     6780edd, i.e. /tmp/fx/gd with the 10 files of 6780edd copied in, or simply main HEAD once snapshot 24 merged).
   - Garden proof at 1920x1080: tour stops 11-13 (Key Coral, Sawgrass, Grove suburb) before/after with tour_multi.sh
     W=1920 H=1080; stop 13 is the important one (lawn along a house: expect crotons/hibiscus along the wall, flowers
     in the beds). Check: no plants on roads/sidewalks/drives/lots/roofs, nothing floating or in water, deterministic.
   - Timing: stops 4-13 at 960x540 with --synctimers --gfxstats, compare with perstop.py (normalise by the lighting
     pass, the runs swing 5-20% with machine load); budget: gbuffer + shadows within +10% at stops 4, 5, 7.
   - Self-test (`NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx EXE=bin/nt_X.exe TIMEOUT=1200 tools/run.sh --gfxselftest`,
     then grep "gfx self-test" /tmp/wine_fx/drive_c/users/root/AppData/Local/NeonTide/log.txt; expect 19 PASS 1 SKIP),
     shc 0 failed, add the phase-2 paragraph to PROGRESS.md (renderer bullet + D3D12 item 2), guarded copy into main,
     sign off with blobs on top of the phase-1 blobs.
2. Phase 3 (tree /tmp/fx/gd3, exe nt_gd3.exe exists): 1080p stops 11 and 13 (palms, hedges) before/after; check the
   new palm fronds read feathery, trunk rings visible but not stripy at distance, coconuts, hedge leaf texture at
   1 m and 10 m; then the same verification + sign-off. Re-sync gd3 with any phase-2 fixes first (diff gd2 vs gd3).
3. Optional measurement promised earlier: `--benchmark` pair (nt_gd0 vs nt_gd1) for CPU "render submit" ms.
4. Clean up test exes (bin/nt_gd*.exe) and BMPs when finished; keep PNGs.
