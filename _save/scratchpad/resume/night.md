# Renderer-night agent resume notes (started 2026-10-06 05:00 UTC)

Task (from the lead): NIGHT LIGHTING REALISM. Phases signed off separately ("renderer-night snapshot-safe:").
A lit interiors through glass (no blow-out, detail, warm/cool variety, not every window lit);
B night exposure + sky (night not dusk, darker bluer sky, moon + stars, moonlight; readable);
C artificial light (lamp pools w/ falloff, sodium+LED mix, shopfront spill, neon bloom, head/tail lights, wet reflections).
Rules: never commit/push/touch refs. Private trees under /tmp/night. Copy to shared tree only at sign-off with the
HEAD-equality guard. Games only via tools/run.sh slot 4 (NT_D3D12_SLOT=1 WINEPREFIX=/tmp/wine_fx), one at a time,
memfree >= 3000 (game) / 2500 (compile), hold while /tmp/neontide_lead_wants < 5 min old. No pgrep/pkill -f.
Base on main after snapshot 36 merges (lead will message). Owned: src/shaders, src/render (keep lighting.hlsl /
dynamic.hlsl character changes: eye-socket fill, strand dither, face-hair bit 3).

## Setup
- /tmp/night/w = HEAD cda29a1 + snapshot 35/36 renderer files (blobs from shared tree) [interim base].
- Scripts copied: /tmp/night/shoot.sh (edit paths!), tour_multi.sh, perstop2.py, imgdiff.sh.
- Showcase night framings (scratchpad/showcase_sets.txt): downtown_street_night 3168.00,-242.90,3.56,-70.00,8.00,21.50;
  downtown_aerial_night 2886.54,-730.50,326.39,-30.00,-30.00,22.00; mimo_motel_night -8201.45,-9279.96,4.22,3.24,0.32,21.00;
  westbrook_pools_night -1283.07,-946.51,54.28,20.00,-45.00,21.50; calle_luna_street_evening 1771.00,357.10,5.47,-65.00,3.00,20.50;
  mimo_strip_mall_evening -1584.69,-591.72,8.14,-96.93,-1.88,20.50. Lead showcase: --quality 3 --settle 16 1920x1080 (~7 min/shot).

## Findings so far (code)
- Exposure: post.cpp render(): comp 0.3-0.55*night; minEV lerp(-3,12,sunElev); post.hlsl csExposure: trimmed geo
  mean, auto-key (<=+1 EV darker), highlight constraint (p97 non-sky <= 3.5 pre-exposed, pull 0.35..0.65), sky constraint.
- Facade interiors (facade.hlsli): lampNits homes 9, shops lerp(160,14,night) x (0.45..1.35); shop ceiling panels 3.0
  (x lampNits); emissive = em*glassTint*1.2. Real modelled interiors (src/world/interior*.cpp, city agent) lit by own
  lights (downlight 380, troffer 760...) -> the downtown lobby blow-out is a modelled interior (InteriorVolume).
- Moon: renderer.cpp computeSunAndSky lightTOA = (0.75,0.82,1)*3 lux (real 0.1-0.3); moon disk radiance 1.2 (tiny).
  Night sky: airglow 0.0012-0.0022 + cityGlowRadiance. Stars starField*0.05.
- Street lamps (world/roadmesh.cpp, city-owned): 7000 cd @ 8.6 m (95 lux below), radius 30, cone 0.2; warm 2/3, cool 1/3.
  Storefront spill (render/world_render.cpp addStorefrontLights): 170 cd, r 8, type 5.
- Local light atten: win^2/max(d2,0.3) * angular. Ambient = sky SH (moon-lit sky LUT + city glow) blended with probe SH
  near the camera (probe sees lit emissive -> one-bounce fill).

## Plan phase A (implemented in /tmp/night/w, exe nt_n1 building)
A1 local exposure (post.hlsl csLocalExpGrid/csLocalExpBlur + localExposure() in psTonemap; post.cpp runLocalExposure,
   lxRaw/lxGrid 3D R16G16F (W/32 x H/32 x 16 bins), lxMean 2D): bilateral grid of log lum; base above pivot
   (pre-exposed 0.35) scaled by gPost3.x = lerp(1, 0.5, night) (1 = off by day: day images unchanged); small bright
   things lean to the 5x5-tile mean (no neon dimming), dark pixels keep own base (no halo). csExposure meters the
   compressed log values (lxCompressAbs) incl. p97/p88 highlight constraint.
A2 facade.hlsli: RoomLamp (ceiling fixture / corner table lamp / office troffers / TV only) + roomLampLight
   (inverse-square + bounce) + visible shade/panel/TV glow; lampCCT(K) colours (homes 2700-3000 K, some 4000+,
   table lamps 2200-2900 K, offices 3500-5200 K, TV flicker blue); levels homes 10*2^(+-1) (table 0.55, TV 0.3),
   offices 12*2^(+-0.5); lamp light through tinted glass half-desaturated tint; homes occupied litFrac*0.75 (was
   0.85); bookcase box; muted product/garment colours. Day term unchanged (room albedo * depth dimming * dayE).
Measure: /tmp/night/imgstats.py img regions (fractions) -> mean, Y percentiles, white/clip/dark shares, saturation.
Baseline showcase stats: downtown lobby region Y p50 0.58 p90 0.73 sat 0.07 (flat near-white); street p50 0.018;
sky (0.021,0.024,0.040). Motel facade p50 0.35 (floodlit), mall grass Y 0.073 > lot 0.009 (moon backlight on grass).

## Log
- 05:00 memfree -307, lead_wants fresh: holding launches; reading code.
- 06:30 b0 baseline done (/tmp/night/shots/b0_*.png; EVs: street 3.34, aerial 1.11, motel 2.51, pools 0.50, calle 3.74,
  mall 2.61). n1 (phase A, nt_n1) shots running via /tmp/night/chain_n1.sh -> marker /tmp/night/chain_n1.done.
  Phase-B tree /tmp/night/wb (= w + B: night EV floor 1.8 [post.cpp minEV], neutral WB at night, mesopic vision in
  psTonemap, stars pixel-sized/brighter, moon disc 260 cd/m2 + aureole, airglow bluer, foliage moon backlight 0.3);
  build nt_b1 queued (/tmp/night/buildb.sh b1 -> build_b1.done). HEAD now 9e7ace0 (snapshot 35 merged); snapshot 36
  (lighting.hlsl, dynamic.hlsl) still compiling: lead will message; then rebase A onto merged tree.
  Scripts: gate.sh (lead flag + memfree), shoot.sh (SHOTS EXE W H Q SETTLE), build.sh/buildb.sh, imgstats.py,
  shc/shc_d3d12.sh TREE /tmp/wine_fx (150 shaders).
- 06:40 Lead item (faces agent): 1-px pale line above dark brows / lash lines at 1 m = tonemap sharpening overshoot.
  Fix in w + wb post.hlsl psTonemap: sharpened value clamped to the cross neighbourhood's min/max. Isolated A/B:
  /tmp/night/shd_sharp/post.hlsl (HEAD post.hlsl + clamp) with nt_n0 --shaderdir; facecam runs s0 (base) / s1 (fix)
  via /tmp/night/chain_fc.sh (after n1) -> /tmp/night/fc/{s0,s1}_*.png, marker chain_fc.done. Args fc_sharp.args
  (c27/c38 1 m day, c27 0.5 m day, c27 1 m night). Waiters armed: chain_n1.done, build_b1.done, chain_fc.done.
- 06:50 BASE READY: main 95dc026 (PR #36). Phase A rebased: /tmp/night/a = archive 95dc026 + w's post.hlsl, post.cpp,
  facade.hlsli (main's versions of those == w's bases). Pristine base tree /tmp/night/m95 -> building bin/nt_m95.exe
  (sign-off "before" exe; marker build_m95.done). Snapshot 37 (world, face.cpp, tests) touches none of my files.
  Phase-A sign-off pair: nt_m95 (before) vs nt_a<N> built from /tmp/night/a (after). PROGRESS.md bullet goes onto
  main's PROGRESS.md (bd82595). wb (phase B) still on cda29a1-era tree: rebase onto a later.
- 07:30 n1 (nt_n1, A first params) OOM-killed after shot 1: downtown street lobby only slightly darker (p90 0.73->0.61),
  tower windows now warm/varied (good). Lobby = white room, flat ambient, ~0.9 pre-exposed (not clipped): needs stronger
  compression. Variant a2 = shaderdir shd_a2 (pivot 0.25 = kTP -2, scale 0.4) on nt_n1 -> chain_a2.sh (6 views).
  nt_b1 built (wb: A orig params + B). Variant b2 = nt_b1 + shd_b2 (wb post.hlsl + a2 constants) -> chain_b2.sh after a2.
  Queue order: fc s0/s1 (sharpening) -> a2 -> b2; nt_m95 build waits for memory.
- 09:30 a2 results (nt_n1 + shd_a2): lobby p50 0.58->0.41 (sRGB 0.79->0.68), detail visible; tower windows warm, but
  brighter/denser than b0 (glass tint half-desaturated). Strip mall products muted. A FINAL params in /tmp/night/a:
  lxScale lerp(1,0.4,night), pivot -2 (0.25); homes occupied litFrac*0.65; lamp level 10*2^(3h-1.8); CCT cool share
  25%. Sharpening clamp included. Building nt_a1 (build_a1.done). Sign-off chain /tmp/night/chain_signA.sh (after b2):
  selftest a1 -> night sa (a1) / sm (m95) -> day da / dm -> tours tm / ta (stops 4-8) -> chain_signA.done.
  Facecam sharpening result: overshoot >4/255 pixels 0.09->0.03% (c27 1 m), 0.15->0.07% (c38 1 m).
- 09:33 Trees: a = final A (sign-off candidate), b = a + B draft (patches /tmp/night/patchB_*.diff from w->wb),
  c = b + C draft: world_render.cpp computeLampArms (pole->lamp arm per type-0 light at upload) + streetLampColor
  (160 m grid hash: 38% sodium (1,.46,.11), 24% 3000K, 24% 4000K, 14% 5000K at the world's luminance) + road-lighting
  distribution (spotInner 3 marker; lights.hlsli streetLampPattern: nadir .55, peak ~60 deg, cut-off 80 deg, house
  side 0.32); props.hlsl lampColorAt (lens colour, alpha-0.6 lenses); particles.hlsl/.cpp cone for the marker.
  All trees compile (150/0). b2 shots + nt_a1 build running now.
- 10:30 nt_a1 built (rc 0). b2 (B draft) results: EV floor works (aerial 1.17->1.80, pools 1.29->1.80): darker, windows
  pop, suburb reads night; lamp strips still broad (C). Sign-off chain A running (selftest next). Queued debug runs
  chain_dbg1.sh (nt_b1, --debugsplit 3 ambient / 8 aerial persp., views aerial, motel, beach_moon_night =
  5078.76,1502.16,3.48,-127.2,25,20.5 [moon ESE 47 deg at 20.5 h]) -> shots g3_*/g8_*. Waiters: dbg1, sa.done.
- 10:40 HEAD b49bc7f (snapshots 37-39): none of my files changed vs 95dc026. City agent will gate facade.hlsli:222
  (storefront = ground && flags&1) on a street-facing mesh mark (snapshot ~41). Told lead: A doesn't touch 222; my
  facade edits = HEAD lines 47-85, 98, 113, 304-374; at sign-off base on HEAD then (3-way merge if their change landed).
- 12:30 Self-test nt_a1: 0 failures, 0 debug-layer errors (log /tmp/night/log_a1_selftest.txt). sa (A after night
  shots) running (machine loaded: ~1 h per 6 views). g3 debug: night ambient near camera = probe SH, warm (~2 lux at
  the motel) -> B: probe weight lerp(1,0.6,night), falloff lerp(280,80,night) (renderer.cpp; trees b and c).
  beach_moon_night view was blocked by buildings: use moon_ocean_night 5400,1500,40,-120.2,25,20.0 (moon ESE 42 deg).
  Building nt_b3 (tree b). Waiter: sa.done + build_b3.done.
- 13:15 sa (A after night) done: EVs 3.23/1.17/2.48/1.33/3.66/2.62; downtown towers are office-flag facades
  (unaffected by the homes tuning; later idea: office brightness spread 2 stops, dimmer evening-only offices).
  sm (m95 before night) running; b3 test (chain_b3.sh: night views + moon_ocean_night) queued (interleaves).
  /tmp/night/proofsheet.sh OUT BEFORE AFTER views... for the proof sheet.
- 13:20 WIND-DOWN from lead: finish A and B to sign-offs (no hurry, full checks); C (street lamps, neon, headlights)
  NOT to be started: notes only. Sharpening clamp is already part of A (tested). After the last sign-off: update
  this file (signed off / unfinished + where / next steps / how to resume), make sure nothing of mine runs, send
  "night done, nothing running". /tmp/night is included in the save.
- 13:56 Worker restart ~13:50 killed only my waiters; chains alive: chain_signA.sh (pid 12008, shooting sm on nt_m95)
  and chain_b3.sh (pid 3012, gated). Waiter re-armed on sm.done / chain_b3.done. Find my processes with:
  ps -eo pid,ppid,etime,args | awk 'index($0, "/tmp/night/") > 0 && index($0, "awk") == 0'
- 14:20 sm done (nt_m95 night). A night before/after (sm -> sa): downtown lobby p50 0.573 -> 0.414 (lin), p90 0.728
  -> 0.530; sidewalk 0.018 -> 0.023; frame p99: street .736->.576, motel .770->.587, pools .669->.587, mall .532->.472;
  aerial p99 .636->.686 (windows a bit brighter). Proof sheet /tmp/night/proof_A_night.png. Next: da, dm (day), tm,
  ta (tours) in chain_signA; b3 interleaves.
- 15:25 b3 (B test, nt_b3) done: aerial/pools darker (EV floor 1.8), mesopic/neutral WB, grass no longer moon-glows,
  moon_ocean_night: moon disc + glow + stars, zenith (0.048,0.062,0.099) sRGB. B FINAL = tree b as built (nt_b3).
  chain_signB.sh queued (waits for chain_signA.done): selftest b3, sa_moon (nt_a1 moon), db (nt_b3 day), tour tb.
  B night before/after = sa vs b3 (+ sa_moon vs b3_moon_ocean_night). shc trees a, b: 150/0.

## PARKED (urgent stop ~15:30 UTC): nothing signed off, nothing running
- Phase A (local exposure, lit interiors, sharpening clamp): tree /tmp/night/a (= main 95dc026 + post.hlsl, post.cpp,
  facade.hlsli; HEAD b49bc7f had the same versions of those files). Exe bin/nt_a1.exe; base exe bin/nt_m95.exe.
  Done: self-test 0 failures; shaders 150/0; night before/after sm vs sa + proof /tmp/night/proof_A_night.png;
  facecam sharpening check (/tmp/night/fc). STILL TO RUN: day shots (. views.sh; SHOTS="$DAY" EXE=... shoot.sh da/dm),
  timing tours (tour.sh tm bin/nt_m95.exe 4 5; tour.sh ta bin/nt_a1.exe 4 5; python3 perstop2.py logs), PROGRESS
  bullet, guarded copy (HEAD equality; city agent may gate facade.hlsli:222 meanwhile -> 3-way merge), sign-off msg.
- Phase B (night EV floor 1.8, mesopic, neutral WB, stars, moon disc+aureole, night probe ambient, foliage moon
  backlight): tree /tmp/night/b (= a + B), exe bin/nt_b3.exe; night test shots b3_* (+ moon_ocean_night) look good.
  STILL TO RUN: chain_signB.sh steps (selftest b3, sa_moon, db day, tour tb), then sign-off vs nt_a1.
- Phase C (not started; draft only, untested): tree /tmp/night/c (= b + street-lamp road distribution + arm
  directions, sodium/LED colour mix per 160 m grid, lens colour in props.hlsl). Also: office window brightness
  spread / dimmer evening offices (facade.hlsli). Then neon glow, headlights/taillights (gameworld.cpp is not mine).
- Shader override dirs: /tmp/night/shd_a2, shd_b2, shd_sharp (test variants only). Next steps in order: finish A
  sign-off, B sign-off, then C. Resume: slot 4 (NT_D3D12_SLOT=1), WINEPREFIX=/tmp/wine_fx, scripts in /tmp/night
  (gate.sh, shoot.sh, build.sh/buildb.sh, selftest.sh, tour.sh, proofsheet.sh, imgstats.py, shc/shc_d3d12.sh).

## RESUMED 10-12 (main 894e7fb; renderer files and PROGRESS.md unchanged vs 95dc026)
- Disk tight: deleted nt_n0/n1/b1 exes, trees w/wb/m95, old shot PNGs (n1, a2, b2, b0). da (A after day) had finished
  before the stop; sa/sm night intact. Tree /tmp/night/ha = archive 894e7fb + A files (sign-off base), building nt_ha.
- Chain /tmp/night/chain_resume.sh (setsid): dm -> tours tm, ta (-> resume_A.done) -> selftest + one night shot on
  nt_ha (-> resume_HA.done) -> B runs: selftest b3, sa_moon, db, tb (-> resume_B.done).
- A sign-off: base HEAD 894e7fb, files post.hlsl, post.cpp, facade.hlsli (+ PROGRESS.md bullet); list facade lines.
  Then B (tree b on top of A; for sign-off rebase onto HEAD-with-A). Then C (tree c draft; relay aviation-light needs).
- 14:45 dm done except mimo_strip_mall_day (OOM-killed): rerun queued (chain_dm2.sh -> shots dm2_*, after resume_A).
  Day A before/after: only edge ringing differs (sharpening clamp) + furniture in daylit rooms; RMSE 0.25-2%,
  visually identical (crop _cmp_day_aerial). nt_ha (HEAD 894e7fb + A) built rc 0. PROGRESS bullet drafted in
  /tmp/night/ha/PROGRESS.md (copy /tmp/night/a/PROGRESS.md.A). Tour tm gated/running.
- 16:00 Tours done: tm (nt_m95) vs ta (nt_a1), stops 4-8, /tmp/night/perstop3.py: gbuffer -0.6%, post +10.5% summed
  (night stops +4%/+22%, day stops +13/+15% with local exposure off = noise), total +2.8% (all normalised by lighting).
  Guard OK for post.hlsl, post.cpp, facade.hlsli, PROGRESS.md (shared tree == HEAD 894e7fb). New blobs (from
  /tmp/night/ha): post.hlsl 502a79bd, post.cpp 351e24c1, facade.hlsli d4fae174, PROGRESS.md 630aebd5. Waiting:
  dm2 (strip mall day before) + nt_ha selftest/shot -> then copy + sign off A.
- 16:20 MAIN MOVED to 61a3b7b (PRs #43/#44: MSVC build fixes; gfx_context.cpp 6 lines, menus, app.cpp, bits.h; CI
  builds with MSVC cl /std:c++17 /W4 -> my C++ must be MSVC-clean: no GCC builtins/VLAs/attributes/M_PI). None of
  my files changed: A blobs stand. New tree /tmp/night/h61 = 61a3b7b + A (+PROGRESS bullet); chain_h61.sh builds
  nt_h61, then selftest + downtown night shot (after resume_HA.done). A sign-off base = 61a3b7b.
- 17:30 dm complete (dm2 strip mall). Day RMSE dm vs da: 0.25/2.1/0.95/1.25/1.25/2.2% (edges; shop products muted by
  day too). nt_ha (894e7fb+A): selftest 0 failures, downtown night shot == sa (lobby p50 .4156 vs .4141, EV 3.21).
  Proofs: /tmp/night/proof_A_night.png, proof_A_day.png. Waiting chain_h61 (61a3b7b+A build/selftest/shot), then
  guarded copy + "renderer-night snapshot-safe:" for A. B runs continue in chain_resume (selftest b3 now).
- 18:52 PHASE A SIGNED OFF: copied into shared tree (guard OK) on HEAD 590854d; blobs in
  scratchpad/renderer_nightA_blobs.txt. nt_h61 (61a3b7b+A): selftest 0/0, night shot == sa. shc 150/0.
- 19:20 Lead verified A (snapshot with AI sets 14-16 after PR #45). B: tree /tmp/night/hb = h61 (61a3b7b+A) + B files
  (post.cpp, post.hlsl, skycommon.hlsli, lighting.hlsl [foliage moon lobe only], renderer.cpp) + B PROGRESS bullet.
  B runs: selftest b3 0/0, sa_moon done (A: faint moon, no stars; B: bright disc + glow + stars), db running, tb next
  (chain_resume -> resume_B.done). chain_hb.sh builds nt_hb then selftest + moon shot after resume_B.
  B sign-off copy must wait until A is in HEAD (guard) or base on A's blobs.
- 20:15 B day (db) vs A day (da): RMSE 0.04-0.33%, motel 1.3% = edge/foliage timing noise (B's code paths are night-
  gated; by day identical values). B night (sa -> b3): median Y street .0351->.0314, aerial .0169->.0083, pools
  .0103->.0047 (dark share .136->.382), motel .0092->.0083, calle .0359->.0354, mall .0129->.0135; saturation down
  (motel .70->.51); EVs aerial/pools 1.17/1.33 -> 1.80. Proofs /tmp/night/proof_B_night.png (+moon row), proof_B_day.png.
  Waiting: tour tb + chain_hb (nt_hb selftest + moon shot).
- 22:35 PHASE B SIGNED OFF (message sent), blobs in scratchpad/renderer_nightB_blobs.txt (tree /tmp/night/hb = 61a3b7b
  + A + B). NOT copied into the shared tree yet (A uncommitted there): copy when A merges or lead says "copy B"; guard
  = shared post.hlsl/post.cpp/PROGRESS.md == A blobs (502a79bd/351e24c1/630aebd5), skycommon/lighting/renderer == HEAD.
  Next: phase C on tree hb (port /tmp/night/c's C diffs: world_render.cpp, lights.hlsli, props.hlsl, particles.*).
- B approved by lead and COPIED into the shared tree (guard OK; snapshot 46 after 45 (A) merges + showcase).
- PHASE C in progress: tree /tmp/night/hc = hb + C draft (world_render.cpp lamp arms + colour mix, lights.hlsli
  streetLampPattern, props.hlsl lens colour, particles.hlsl/.cpp) + facade.hlsli: offices out of hours x0.45 and 2-stop
  spread, aviation beacons on facades > 60 m (pattern-1 rhythm, phase per building). chain_c1.sh: build nt_c1 -> c1
  night views -> tour stops 7-8 at 1080p on nt_hb (vb) and nt_c1 (vc). Machine ~2 GB free: one game at a time.
  Aviation relay for lead: Solaris One top, rural smokestacks, stadium light masts, bridge towers (> 60 m) need
  beacons (emissive red box MAT_EMISSIVE pattern 1 + type-4 light, like buildmesh.cpp rooftopAntennas).
- 23:20 nt_c1 built (rc 0; embedded the beacon code before the pattern-1 tweak). c1 shots OOM-killed (machine ~2 GB
  free): chain_c1b.sh relaunched with GATE=4000 (shoot.sh / tourshot.sh take GATE env) -> c1 night views, then tour
  stops 7-8 at 1080p: vb (nt_hb), vc (nt_c1).
- 01:30 C first look (c1 BMPs): poles no longer floodlit by their own lamp (house side 0.32), sodium blocks read orange
  (downtown street near block = sodium), suburb pools tighter. Waiting for calle/mall + tour stops 7-8 (vb/vc).
- 01:25 c1 1080p run OOM-killed again after 4 views (crowded machine). C evaluation now at 1280x720 Q2:
  chain_c1c.sh: e2b/e2c (calle + mall on nt_hb / nt_c1), tour stops 7-8 vb/vc; then chain_c2.sh: beacon check
  (aerial, shd_c2 = hc facade with beacons held on). Aerial C: office windows more varied/dimmer out of hours (good);
  beacons not visible in c1 aerial (flash off-phase likely; check bcn).
- 02:40 Snapshot 46 = 67e6159 (A+B in main): C bases now 67e6159. Lead item (showcase 1013_0153 strip mall): shop
  interiors flat white boxes -> hc facade.hlsli shopInterior: ceiling between panels 0.82->0.40, walls vertical light
  gradient (0.6 floor .. 1.0 near ceiling), depth falloff to 0.45 by 1.3 D, brand stripe x0.7, garments darker and
  muted. Test: chain_c3.sh (nt_c1 + shd_c3 = hc facade) -> e3c_* (mall + downtown street) after chain_c2 (bcn).
  vb tour OOM-killed after stop 7; vc (nt_c1 stops 7-8) running. Calle Luna wall light = a type-1 building light
  (city data), unaffected by C.
- 03:20 Lead HOLD until 03:55 (AI knock run gates PR #46): gate.sh waits while /tmp/night/HOLD exists;
  hold_until.sh 0355 removes it. bcn (nt_c1 720p aerial, beacons held on) is the current run. vc done (stops 7, 8).
- 03:30 bcn (beacons held on) showed NO beacons: facade uv.x of wall k starts at (floor(k*1000/bay)+0.002)*bay
  (buildmesh.cpp facadeWalls), not 0. Fixed in hc: beacon at each of walls 0-3 starts. shd_c2/shd_c3 refreshed.
  Queue (behind HOLD until 03:55): chain_c3 (e3c: mall + downtown street, shop interiors) -> chain_c4 (bcn2).
- 05:15 HOLD re-armed until 05:45 (AI build + copreturn for PR #46). bcn2: red beacons now visible at roof corners
  (1-2 px at 720p). e3c: rails muted but shops still bright boxes (view from 8 m looks at the floor) -> hc: floor
  0.52/0.42, wall gradient 0.5..1, depth falloff to 0.35 by 1.2 D; retest e4c (chain_c5, after hold).
  HEAD now ab5995c (A, B in main; C bases = hb's = HEAD's). C SIGN-OFF TREE /tmp/night/cF = ab5995c + 6 C files +
  PROGRESS bullet; base tree /tmp/night/m59. chain_signC.sh queued: builds nt_cF, nt_m59 -> selftest -> night nc/nm,
  day dc/dmm (GATE 4000) -> tours tm59/tcF. Deleted trees a, b, c, ha, h61 and exes m95, a1, b3, ha, h61.
- 06:10 WIND-DOWN (lead): finish C properly (sign off if shippable, else park), then start nothing new; shop interiors
  stay PARKED: removed from cF (facade.hlsli = HEAD + office lighting + beacons only); the shop edits are saved in
  /tmp/night/park_shop_interior.diff (apply onto cF's facade.hlsli; e3c showed muted rails, e4c retest was running
  orphaned at 06:00, BMP in /tmp/night/shots/e4c/ if it finished). HOLD expired 05:45 (file gone). chain_signC.sh
  relaunched (builds cF + m59 -> selftest -> nc/nm night, dc/dmm day at GATE 4000 -> tours tm59/tcF).
- 06:30 nt_cF + nt_m59 built (rc 0); selftest cF 0/0. Parked shop retest e4c saved (shops still read bright: the
  walls above the rails dominate; parked note). nc (C night, 1080p Q3) running; then nm, dc, dmm, tours.
- 07:20 nc OOM-killed after 3 views. New helpers: shoot1.sh (one game per view, 3 tries), tour1.sh (retry tours).
  chain_signC2.sh: nc missing 3 views (pools, calle, mall) -> nm (6) -> dc/dmm (6+6 day) -> tours tm59/tcF.
- 09:55 C night before/after complete (nm vs nc, HEAD ab5995c ± C): motel facade p90 .221->.126 (no longer
  floodlit), pools p90 .087->.058 (tighter pools), aerial p90 .056->.043 (offices dimmer/varied), street column + pools
  sodium orange, strip mall lot sodium (sat .27->.39), calle unchanged (building light). Proof /tmp/night/proof_C_night.png.
  Next in chain_signC2: dc/dmm day, tours tm59/tcF.
- 11:45 C day before/after (dmm vs dc): RMSE <= 0.065%, strip mall identical. Proof /tmp/night/proof_C_day.png.
  Tours tm59/tcF running (chain_signC2 -> chain_signC2.done).
- 12:20 C tours tm59 vs tcF (stops 4-8): run-to-run load differed (raw total 8555 -> 6715 ms, C faster); normalised
  by lighting +8.4% total but shadows/envprobe +19/+26% at DAY stops too (normaliser bias: C changes nothing there).
  Within-run night/day ratios: lighting 1.45 -> 1.32, fog 1.57 -> 1.48, total 1.156 -> 1.035: no night cost from C.
  HEAD moved to 8993054 (city: storefront gate streetMark + landmark beacons): facade.hlsli 3-way merged cleanly
  (/tmp/night/merge; C hunks at HEAD lines 478, 480, 549-565). Final tree /tmp/night/c99 (8993054 + C), shc 150/0;
  chain_c99.sh: build nt_c99 -> selftest -> aerial + street shots (c99_*). Then guarded copy + sign-off + parked note.

## PARKED 10-13 12:45 UTC (wind-down): nothing running, no HOLD file
Signed off (all three, blobs in scratchpad/renderer_night{A,B,C}_blobs.txt):
- A (local exposure, lit interiors, sharpening clamp): merged (c1af87a).
- B (night EV floor 1.8, mesopic, neutral WB, stars, moon, night probe ambient, foliage moon lobe): merged (67e6159).
- C (road-lighting distribution + arm detection, sodium/LED mix + lens colour, aviation beacons on facades > 60 m,
  office light variety): copied into the shared tree on HEAD 8993054 (facade.hlsli 3-way merged with the storefront
  gate); sign-off sent 12:40; awaiting the lead's snapshot. Final tree /tmp/night/c99, exes bin/nt_c99 (HEAD+C) and
  bin/nt_m59 (ab5995c base).
Parked / next, in order:
1. Shop interiors (lead note, parked): /tmp/night/park_shop_interior.diff applies onto C's facade.hlsli (darker
   ceiling between panels, wall gradient, depth falloff, mid-grey floor, muted rails). Rails better; shops still read
   bright (walls above the rails dominate): try a lower night shop level or the walls' albedo; tests e2c/e3c/e4c PNGs
   in /tmp/night/shots (strip mall, 720p).
2. Neon glow/bloom tuning at night; headlights/taillights (vehicle lights are submitted by game/gameworld.cpp, not mine).
3. Police HQ lobby still reads pale: its interior lighting is flat (city data; ambient-dominated).
4. Optional C follow-ups: sodium share (38%) via streetLampColor + props.hlsl lampColorAt; beacons on round towers.
How to resume: games only via tools/run.sh slot 4 (NT_D3D12_SLOT=1, WINEPREFIX=/tmp/wine_fx), one at a time, memfree
gate (1080p Q3 runs need ~4 GB: GATE=4000 and shoot1.sh = one game per view with retries; tour1.sh retries tours).
Scripts /tmp/night: gate.sh (honours /tmp/night/HOLD), shoot.sh, shoot1.sh, tourshot.sh, tour.sh, tour1.sh, buildb.sh
TAG TREE, selftest.sh, proofsheet.sh, imgstats.py, perstop3.py/perstop4.py, shc/shc_d3d12.sh TREE /tmp/wine_fx, views.sh
(NIGHT, DAY, MOON lists). Base a new tree on `git archive HEAD` and guard copies (file == HEAD) at sign-off.

## RESUMED 10-19 13:30 UTC (HEAD 0d67933 = C in snapshot 51, PR #47 open; main af8a066 = v0.02.6)
Tasks: 1 shop interiors (park diff + walls above rails), 2 neon bloom, 3 sodium share (streetLampColor ==
lampColorAt), 4 round-tower beacons, 5 vehicle lights: PROPOSE gameworld.cpp changes to the lead first (not mine),
6 type-4 CPU blink vs shader pattern-1 sync: agree with the city agent if asked. Lead showcase rendering: memgate,
one game at a time. Tree /tmp/night/d = archive 0d67933 + park_shop_interior.diff (applied, fuzz 1).
- 14:25 Aviation sync agreed (lead relayed the city agent's proposal): tree /tmp/night/av = 0d67933 + world_render.cpp
  case 4 (frac(time*0.8 + li.phase/256) < 0.18; Reduce Flashing: (0.55+0.25 sin(time*1.2+ph*2pi))/1.6) + member
  WorldRenderer::reduceFlashing set in renderer.cpp before gatherLights + the CITY's worldtypes.h (blob 63a9e6e3,
  base 21d26d26 = HEAD's; NOT mine to copy). Sign off av separately (pair snapshot with the city agent).
  Tree d: shops (dropped ceiling 3.2-4.4 m + plenum, wall finishes, posters/signs/menu boards), sodium 20% (thresholds
  .2/.45/.8 in streetLampColor == lampColorAt), corner walls by facing (xl.z fraction = axis angle in quarter turns;
  beacons + LED lines on 4 corners incl. round towers). shc d 150/0. chain_d1.sh: build av, d1 -> selftest av ->
  d1 shots (mall, calle, street; 720p) -> bc3 (held-on beacons, aerial) -> av aerial. Bloom: next (shader-only).
- 16:05 HEAD = b71d339 (PR #47 merged + animator.cpp stairs fix): none of my files changed; bases still = 0d67933's.
  nt_av + nt_d1 built rc 0 (waited ~1.5 h on the shared build lock); selftest av 0/0. av blobs (hash-object -w):
  world_render.cpp 2de3ba98, renderer.cpp 0d38d93f (worldtypes.h is the city's 63a9e6e3: they copy it).
  chain_d1.sh stopped while gated (no game started) and replaced by chain_d1b.sh: av aerial (-> av_shot.done) ->
  d1 shots -> bc3 -> chain_d1.done (chain_bl1, chain_hl1 wait on it). Variants: shd_bl1 (post.hlsl softer Karis at
  night), shd_hl1 (lights.hlsli low-beam: cut-off 0.7 deg above aim, foreground falloff, peak 1.6x). shc 150/0 both.
- 17:00 av SIGNED OFF (copied to shared tree, guard OK; msg sent). Tree d now = d + av merged (world_render.cpp 3-way clean, renderer.cpp = av's, worldtypes.h = CITY's 63a9e6e3 for compiling only: never copy it).
- 17:10 Lead accepted the av half (snapshots with city batch 17); lead put worldtypes.h 63a9e6e3 into the shared tree.
  RULE (lead): before copying a file that depends on another agent's UNLANDED change, tell the lead first (my
  world_render.cpp broke everyone's shared-tree syntax check until worldtypes.h landed).
- 19:45 Results (720p): d1/d2 shops much better (HEAD h0a vs d2 mall: white boxes -> varied finishes, signs, posters;
  bl1 beach_street_night close-up: charcoal wall + troffer ceiling + pink dept signs + cafe menu boards). Tweaks in d:
  upper walls x0.6, pale finish 0.5, brand finish lerp(.3,brand,.45)*.45, ceiling between panels 0.25, night shop
  level 14 -> 11 nits, lettering of dept signs varied. Bloom: softer Karis at night (kk = lerp(1, 1/64, night))
  ADOPTED into d's post.hlsl (neon letters glow more; no haze, means unchanged). bc3: beacons/LEDs at corners
  (python check: 20-gon walls 0/5/10/15, rect 4, chamfer 4 sides). Calle luna's left shop row lost its storefront
  in HEAD (city streetMark gate), not mine: mention to lead. veh1/veh2 (night traffic, low-beam variant) running.
  shc d 150/0.
- 20:00 DIRECTION CHANGE (user via lead): procedural world -> entirely hand-built world (terrain kept; every building
  enterable). "Finish, then switch": finish night-traffic runs + headlight proposal (vehicle lights stay); DROP
  generated-city items (shop interiors, sodium share per block, round-tower beacons/corner LEDs) -> parked as
  /tmp/night/park_generated_city.diff (vs 0d67933; world_render.cpp hunks include av's merged case 4 = already signed
  off, ignore those); neon bloom (post.hlsl softer Karis at night) -> sign off (+ lights.hlsli low beam if veh2 good).
  Then PARK and wait. Future: lighting for hand-built interiors (every room lit, many lights per building, perf with
  thousands of enterable buildings); city agent's architecture proposal comes first.
- 21:30 veh1/veh2 (night traffic 720p, A/B low beam) done (timed out at t~10 s, 2 shots each): new pattern removes
  the hot white patch in front of the bumper (road 193 -> 123 sRGB), far road slightly brighter: ADOPTED.
  Vehicle-light proposal (gameworld.cpp: LED/halogen mix + bit 64, tail 60->15 cd, reverse light, siren pulse with
  Reduce Flashing, pair merge >40 m + cap 24, fallback aim -0.03) SENT to the lead; nothing touched.
  Sign-off tree /tmp/night/e = archive 731a731 + post.hlsl (softer Karis at night) + lights.hlsli (low beam) +
  PROGRESS bullet ("Neon glow and low beams at night"). shd_head = HEAD's post.hlsl + lights.hlsli (the A/B
  "before" on the same exe). chain_e.sh: build nt_e1 -> selftest -> night A/B eb/ea (NIGHT + beach, 1080p Q3,
  one game per view) -> day A/B edb/eda (3 views) -> tours teb/tea (stops 4-8) -> chain_e.done.
- 22:30 Lead GO: all six vehicle-light items myself (gameworld.cpp + dynamic.hlsl halves). Combined sign-off with
  bloom + low beam. Tree /tmp/night/v2 = 731a731 + post.hlsl + lights.hlsli + gameworld.cpp (headlampIsLED by model
  -> bits 64, kHeadLED/kHeadHalogen, tail 15 cd r 2.5, brake 220 r 5 (tails act as brake lamps when a spec has no
  LT_BRAKE), reversing spot 80 cd r 4.5, siren pulse with reduceFlashing, cap 24 + pair merge cross-fade 36..44 m,
  fallback aim -0.03) + dynamic.hlsl (lens LED/halogen by bit 64, siren lens pulse with gRenderFlags.x) + PROGRESS
  bullet (combined). LOCAL-ONLY app.cpp tweak in trees e and v2 (parking honours --time; lead: keep app.cpp OUT of the
  sign-off, say the night reverse shot came from a local tweak; AI agent adds it in set 18). chain_e killed (its e1
  build kept as the before exe). chain_v.sh: build v1 -> selftest -> vtA/vtB traffic (synctimers) -> vcB0/vcB1 chase
  RF off/on (settings.ini reduce_flashing=1 in wine_fx, removed after) -> vpA/vpB parking night -> night A/B eb/va
  (NIGHT + beach 1080p) -> day A/B edb/vda (3) -> tours teb/tva. Checks: siren audio has no visual coupling; HUD
  flashing all goes through calm(); indicators/hazards (1.5 Hz) unchanged by RF (functional signals).
- 21:50 Lead: beacon sync complete (city world side signed off, 97/97 in step); av files go in snapshot 56 (after 55 + autoupdater compile). HOLD for upd pid 14554 / 22:30 active.
- 00:05 (10-20) nt_e1 + nt_v1 built rc 0 (no warnings), selftest v1 0 failures. Traffic A/B (vtA nt_e1+shd_head,
  vtB nt_v1; 720p Q2 synctimers, 16 paired timing blocks; /tmp/night/passcmp.py): B/A normalised by gbuffer (load):
  lighting +0.6%, fog -5.3%, total +0.0% (raw -12% = load). 21 AI cars in the scenario. Stills: hot patch gone (f03,
  f05), tails faint cruising (f05/f08), braking glow kept (f00); beam colours B: wagon b/r 1.01 (LED), red SUV 0.82
  (halogen) vs A all 0.91-0.93. Next: chase RF off/on (vcB0/vcB1), parking night A/B (vpA/vpB), night stills, day, tours.
- 02:10 Chase RF off/on (vcB0/vcB1): the cruiser stays behind the camera (no siren in view); HUD calm() visible.
  Parking: the scenario's departing car pulls out forward (traffic.cpp pullOutStep: no reverse) -> parking runs
  stopped (vpA killed by pid, vpB dropped). Second LOCAL-ONLY tweak (lead allowed local test tweaks; never signed
  off): app.cpp drive autoplay `--reversetest`: t 5..10 s brake to a stop then reverse (brake lamps, then reversing
  lamps, camera behind the car). chain_v2.sh: builds nt_e2 (tree e) + nt_v2 (tree v2) -> selftest v2 -> drive A/B
  (vdA/vdB) -> wanted RF off/on (vwB0/vwB1) -> night A/B eb/va 1080p -> day edb/vda -> tours teb/tva. vtA/vtB traffic
  A/B (nt_e1/nt_v1) stay valid (same code bar the inert app tweaks).
- 02:25 CONTAINER RESTART ~02:19 killed chain_v2 during vdA. Done before it: nt_e2 + nt_v2 builds (rc 0), selftest v2
  (0 failures, 0 debug-layer errors). HEAD now f595703 (av landed: world_render.cpp 2de3ba98, renderer.cpp 0d38d93f);
  my sign-off files' bases unchanged (post f678aa56, lights bd09ea7d, gameworld c1bacb64, dynamic ea97d07d, PROGRESS
  e9dd440d). chain_v3.sh relaunched (drive A/B, wanted RF off/on, night/day A/B, tours). NEXT after sign-off (lead):
  leaf-shadow artifact (ring/donut dapples on /tmp/showcase/1019_2228/grove_coral_wall.png): find why, propose a fix.
- 02:45 Leaf-shadow rings: CAUSE found in shadow.hlsli sampleCascade: kPoisson = the classic 12-tap DOF Poisson set
  (meant with a separate centre tap): radii 0.51..0.99 R, no tap within half the radius -> the PCSS filter is an
  annulus; a sunlit leaf gap smaller than the penumbra (r up to 9 texels) is convolved into a ring with a dark middle
  (TAA + per-pixel rotation keeps the annulus). The blocker search (taps b*2) is a ring too. FIX (proposal, tested
  next): 12-tap Vogel spiral r = sqrt((i+.5)/12) (radii 0.20..0.98, same cost) in /tmp/night/shd_vogel/shadow.hlsli
  (shc 150/0). chain_g.sh (after chain_v3): grove_coral_wall 1080p A/B (gA HEAD, gB vogel) on nt_e2.
- 03:40 Drive A/B (vdA/vdB, --reversetest): braking t~4.5 (27 km/h, tyre smoke): both flare red (B brake 220 + tail
  15, A 220+220); reversing t 5.5-10: B lights the road/pavement behind (A none) [vd_05, vd_06, vd_08]; the car
  backed onto the sidewalk into peds -> wanted 3 (scenario artefact). Cruising tails: vt_05/vt_08 (B faint).
  LED/halogen: vtB_03 wagon b/r 1.01 vs vtB_05 SUV 0.82 (A all ~0.92). Pop: cross-fade continuity argument + far-car
  crops. Waiting: vwB0/vwB1 (wanted RF off/on), then night/day A/B, tours, then chain_g (grove shadow A/B).
- 03:45 SECOND RESTART ~03:37 (killed vwB0 mid-run). HEAD 837b39c: my sign-off files unchanged except PROGRESS.md
  (HEAD 6b69c0c0: re-apply my bullet on HEAD's version). chain_v4.sh = RESUMABLE (skips finished steps; relaunch it
  after any restart): vwB0/vwB1, night A/B 4 views (street, calle, beach, aerial), day A/B 2 views, tours, then the
  grove A/B (gA/gB). Reduced view counts because of the restarts (vehicle evidence is the 720p scenario runs).
- 03:55 SIGN-OFF TREE /tmp/night/sf = archive 837b39c + post.hlsl 02a0a4c4, lights.hlsli 025fbf95, gameworld.cpp
  ae7d6657, dynamic.hlsl 1b2b6106, PROGRESS.md cfaf0386 (bullet re-applied on HEAD's 6b69c0c0); verified: only these
  5 files differ from HEAD. Build nt_sf running (orphaned buildb, pid 1479); chain_sf2.sh: selftest sf after
  chain_g.done. AFTER A RESTART: relaunch chain_v4.sh (resumable) and chain_sf2.sh (+ buildb.sh sf /tmp/night/sf if
  build_sf.done is missing). Then guarded copy (5 files == HEAD blobs above) + sign-off message.
- 05:30 THIRD RESTART ~05:15 (HEAD bcb8a71; my 5 sign-off files' bases unchanged: sf tree still valid). Done: vwB0,
  build sf. vwB0 (wanted, RF off): police never in view (player runs ahead) -> vwB1 skipped. chain_v5.sh (resumable,
  no tours: the traffic synctimers A/B vtA/vtB is the timing evidence for the whole change set): selftest sf ->
  night A/B beach, street, calle -> day A/B street, calle -> grove gA/gB. SIREN evidence plan: LOCAL-ONLY autoplay
  "sirentest" in tree v2 app.cpp (police car with siren 9 m ahead at night, shots every 0.1 s), rebuild nt_v2s, run
  RF off/on.
- 05:45 LOCAL-ONLY "sirentest" autoplay in tree v2 app.cpp (patrol car 9 m ahead facing the camera, siren + lights
  on; shots auto_siren_00..15 every 0.1 s from t=3). chain_s.sh: build nt_v2s -> vsB0 (RF off) / vsB1 (RF on, via
  settings.ini). shoot.sh/selftest.sh/chain_s now take /tmp/night/game.lock (flock, fd 8 closed for the game) so my
  chains never run two games at once. After a restart: relaunch chain_v5.sh and chain_s.sh (both resumable).
- 13:50 (after a 4th restart, back 13:31; everything had finished before it) SIGNED OFF bloom + low beam + vehicle
  lights on HEAD bcb8a71 (guard OK, copied): post.hlsl 02a0a4c4, lights.hlsli 025fbf95, gameworld.cpp ae7d6657,
  dynamic.hlsl 1b2b6106, PROGRESS.md cfaf0386. Evidence: /tmp/night/proof_V_night.png, proof_V_day.png,
  proof_V_vehicles.png; timing = traffic synctimers A/B (total +0.0% load-normalised). app.cpp test tweaks
  (parking --time, --reversetest, sirentest) LOCAL ONLY in trees e/v2.
  Leaf-shadow rings: cause = annulus tap set (classic DOF Poisson 12, no tap within 0.51 R) in shadow.hlsli; fix =
  Vogel 12 (/tmp/night/shd_vogel/shadow.hlsli, base c990cc31, blob 72e3a1a3), grove A/B proof
  /tmp/night/proof_leafrings.png, sim /tmp/night/sim/ringsim.png. Proposal SENT; awaiting go to sign off.
  Then PARK: next = lighting for hand-built interiors after the city agent's architecture proposal.
  Nothing of mine running; no HOLD, no settings.ini in wine_fx.
- 14:05 Lead: night set ACCEPTED into snapshot 61 (all five blobs). GO: sign off shadow.hlsli 72e3a1a3 (leaf rings)
  + held brake lamps (on top of gameworld ae7d6657) + a code note where models are added + a night still of a queue
  at a red; then PARK until the city's hand-built interiors proposal. Tree /tmp/night/r = archive bcb8a71 + the five
  night-set files + shadow.hlsli 72e3a1a3 + gameworld.cpp 7df66af0 (held = driven, gear >= 0, |vFwd| < 0.5, throttle
  < 0.05, not aircraft/boat; vFwd = the driver's vDummy for dummy-mode traffic, whose body vel is 0) +
  vehicle_catalog.cpp 5868f0ca (comment only, base HEAD 8e801de0) + PROGRESS.md fcf9f307 (bullet "Dappled sunlight
  and held brake lamps"). shc r 150/0. chain_r.sh (RESUMABLE): build r1 -> selftest -> shadow A/B rb/ra (grove,
  calle day, downtown night; 1080p) -> traffic rtA (nt_sf) / rtB (nt_r1) 720p 22:00 (queue at a red).
  Guards at sign-off: shadow.hlsli == HEAD c990cc31, gameworld.cpp == ae7d6657, vehicle_catalog.cpp == HEAD
  8e801de0, PROGRESS.md == cfaf0386 (or the snapshot-61 versions if 61 commits meanwhile: then the same bases).
- 14:20 City agent's interior lighting proposal arrived (/tmp/city/msg/interior_lighting_note.txt). My reply SENT
  (/tmp/night/msg/reply_interior_lighting.txt): R16_UINT room-id MRT (0 outdoor; decals masked), tLightRoom u16 per
  light, tRooms[id] O(1), per-tile room set (<= 4 ids) filter, fog only outdoor + camera-room lights, photometric
  table in lm/deg/K/emitter area (C++, keyed by code=), per-window u32 records via shell vertex window index +
  building base, integer day/minute in the frame CB for the HLSL lit() + agreement selftest, exposure: room id != 0
  replaces cameraInInterior, gfxstats counters for the pilot. Work starts once the city writes HANDBUILT_WORLD 7.8.
- 14:40 City: 7.8 written (doc blob 415f44c9/c4f763a7, confirmed by me). roomLight() spec arrived: header
  src/world/roomlight.h blob a6fcf2b8 (scratchpad rlt/), 104 vectors (tests/world/worldmodel_test.cpp cec03b2b),
  packed table /tmp/city/msg/roomlight_table.hlsl, sketch /tmp/city/msg/roomlight_hlsl_sketch.txt, spec copy
  /tmp/city/msg/roomlight_spec_to_night.txt. NEXT (after the r sign-off): src/shaders/roomlight.hlsli (uint only;
  regenerate the table myself + diff), renderer self-test (compute sweep + 104 vectors vs World::roomLight, logged),
  Renderer::roomNight() = 255*saturate((4 - sunElev)/10) (+ overcast/rain), Environment u32 gameDay (gameplay sets),
  roomMinute() helper asked from the city.
- 15:05 Tree /tmp/night/rl = HEAD + city roomlight.h (rb tree version 92d3b91c, with roomMinute; UNLANDED: tell the
  lead before copying anything that depends on it) + src/shaders/roomlight.hlsli (table regenerated from the
  header: identical to the city's packing) + roomlighttest.hlsl (csRoomSweep 559,440 inputs, csRoomVectors 104) +
  src/render/roomlight_test.cpp (roomLightSelfTest, logged "room-light self-test: ... -> PASS/FAIL") + renderer.cpp
  include + hook (--roomlighttest). shc rl 152/0. chain_rl.sh: build rl1 -> after chain_r -> one 640x360 shot run
  with --roomlighttest -> /tmp/night/rltest_result.txt.
- 16:50 Lead: real-GPU PC showcase (scratchpad/pc/results/2026-10-20-04) confirms the leaf rings (keep the Vogel
  fix). NEW ITEM after rings + brake lamps: key_solano_pickets_morning.jpg: beige blotches in diagonal rows on a
  corrugated metal roof (shaded half too) -> diagnose first (dirt/detail layer? specular aliasing?). Also: with my
  first roomLight() use, set Environment::gameDay from GameWorld::gameDay in app.cpp (one guarded line, mine). The PC
  runs before/after shots on a real GPU in < 1 min: ask the lead (scratchpad/pc/queue_pkg.sh is the lead's tool).
- 16:45 Speckles hypothesis: MAT_ROOF_METAL (materials.cpp:49, matgen GEN 16 genMetalRoof): rust = smoothstep(0.55,
  0.85, tfbm(uv,3,5,.55)) mixed 70% with colour C (0.32,0.16,0.07), rough 0.85, at the material's constant metal 0.6;
  2.5 m tile repeats the spots in rows; autotest uses 256-texel layers (--texsize 1024 for comparison). Diagnosis
  runs chain_sp.sh (after chain_r + chain_rl): spA as is, spN no rust (shd_norust), spT texsize 1024.

## PARKED 10-20 17:50 UTC (lead: usage almost gone). Nothing of mine running (chains + rtB game killed by PID).
1. Leaf rings + held brake lamps sign-off: NOT sent. Tree /tmp/night/r (HEAD bcb8a71 + night set + changes), nt_r1
   built rc 0, selftest 0/0 (log_r1_selftest.txt), shc 150/0. Blobs: shadow.hlsli 72e3a1a3 (base HEAD c990cc31),
   gameworld.cpp 7df66af0 (base ae7d6657 = snapshot 61), vehicle_catalog.cpp 5868f0ca (comment; base HEAD 8e801de0),
   PROGRESS.md fcf9f307 (base cfaf0386). Shadow A/B done on nt_r1 (rb = HEAD shadow.hlsli via shd_shhead, ra = fix):
   grove (rings -> soft spots), calle day, downtown night: /tmp/night/shots/r[ab]_*.png. Traffic: rtA (nt_sf, no
   hold) done (15 shots); rtB (nt_r1, queue at a red) STILL TO RUN -> rerun only rtB (chain_r.sh is resumable: it
   skips done steps; or ask the lead for a PC run), then proof sheet, re-check bases vs the new HEAD, guarded copy,
   sign-off message.
2. Roof speckles (key_solano_pickets_morning, PC shot): likely the metal roof's rust layer (matgen genMetalRoof:
   fbm-thresholded spots, colour C at 70%, constant metal 0.6, 2.5 m tile repeating in rows; autotest = 256-texel
   layers). Diagnosis runs prepared, not run: chain_sp.sh (spA as is, spN shd_norust, spT --texsize 1024).
3. HLSL roomLight: tree /tmp/night/rl (HEAD + city roomlight.h 92d3b91c [lands in snapshot 63]) with
   src/shaders/roomlight.hlsli, roomlighttest.hlsl, src/render/roomlight_test.cpp, renderer.cpp hook
   (--roomlighttest); shc 152/0; nt_rl1 built rc 0; the test run NOT done (chain_rl.sh: one 640x360 shot run with
   --roomlighttest -> rltest_result.txt). Then: Environment::gameDay + app.cpp guarded line, Renderer::roomNight().

## RESUMED 10-26 15:20 UTC (HEAD bdf3016). Relaunched chain_r (rtB only left), chain_rl (--roomlighttest), chain_sp
## (speckles A/B). PROGRESS.md HEAD is now ca9479c7 (re-apply bullets at sign-off); renderer.cpp HEAD 3a970bc0 and
## app.cpp HEAD a4c4aee9 (rebase tree rl on them); roomlight.h 92d3b91c landed.
- 15:45 Sign-off tree /tmp/night/r2 = archive bdf3016 + shadow.hlsli 72e3a1a3, gameworld.cpp 7df66af0 (bases == HEAD),
  vehicle_catalog.cpp 5868f0ca, PROGRESS.md 0cc998fb (bullet on HEAD's ca9479c7); chain_r2.sh builds nt_r2 + selftest.
  Room light rebased: tree /tmp/night/rl2 = archive bdf3016 + roomlight.hlsli, roomlighttest.hlsl, roomlight_test.cpp,
  renderer.cpp (+include, hook, roomNightLevel = 255*sat((4-sunElev)/10 + cloud/rain terms)), renderer.h
  (Environment::gameDay, roomNight()), app.cpp (+ env.gameDay line; shared app.cpp has another agent's uncommitted
  changes -> tell the lead at sign-off). chain_rl2.sh: build rl2 -> after chain_r2 + chain_sp: selftest + test run.
- 16:15 SIGNED OFF leaf rings + held brake lamps on HEAD 61cf548 (guard OK, copied): shadow.hlsli 72e3a1a3,
  gameworld.cpp 7df66af0, vehicle_catalog.cpp 5868f0ca, PROGRESS.md 0cc998fb; selftest nt_r2 0/0; proofs
  /tmp/night/proof_R_shadow.png, proof_R_heldbrake.png (rtA/rtB frame 14). Offered a PC run for a multi-car queue.
  Running: chain_rl (rl1 test), chain_sp (speckles spA/spN/spT), chain_rl2 (build rl2 -> selftest -> test).
- 16:30 Lead: rings + held brake lamps ACCEPTED into snapshot 64 (tree 3e82f6a6 verifying). app.cpp: snapshot 64 changes it (city hand-mode start, 1a889880 on a4c4aee9): rebase my gameDay line on 64's app.cpp once it lands; keep the hunk one line. Next: speckles, then --roomlighttest.
- 16:50 --roomlighttest PASS (nt_rl1): 5,594,400 sweep inputs (1,751,066 lit), 0 GPU/C++ differences; 104 vectors 0/0. Told the city agent. chain_sp (speckles) next, then chain_rl2 (rebased build + selftest + test).
- 17:00 City FYI (snapshot 65, part 2a): World::gHandBlocks->lampSched = a roomlight class per hand-placed lamp (RS_DUSK photocell, RS_PORCH evening, RS_ALWAYS), beside lamps[i] (src/world/blocksbin.h); cells take them into CellGeometry::lights. Hook for switching lamps by schedule later (gatherLights). Nothing needed now.
- 17:20 SPECKLES DIAGNOSED: spA (as is) shows the beige blotches; spN (rust off) clean; spT (1024-texel layers)
  still blotchy -> the metal roof's RUST layer (matgen genMetalRoof), not specular aliasing / texture resolution.
  Light rust (0.32,.16,.07) at 70%, rough 0.85, at the material's metal 0.6 catches a blur of the bright sky that the
  glossier steel doesn't. Fix variant /tmp/night/shd_rust2/matgen.hlsl: low-frequency mask (tfbm base 2, 4 oct,
  0.6..0.9), less on crests, colour C x0.5 at 60%, rough 0.35..0.6; shc 150/0. chain_sp2.sh: spR after chain_rl2.
- 17:45 Lead approved the rust fix (matgen only); sign-off wants A/B on 2-3 other metal roofs (warehouse at noon, one at dusk) + gfxselftest. chain_sp3.sh: build nt_m1 (61cf548 + matgen) + selftest; A/B rrA/rrB on nt_r2 (+-shd_rust2): the_flats_low_noon, key_solano_low_dusk. Snapshot 64 (PR #57) pending merge; app.cpp line on 1a889880 after it merges (lead tells the commit).
- 19:00 main = 69341af (PR #57: rings + held brakes in). FINAL TREE /tmp/night/f = archive 69341af + matgen.hlsl
  447646b4 (base 3240e731) + roomlight.hlsli 84ab4186 (new) + roomlighttest.hlsl ae632c1a (new) +
  render/roomlight_test.cpp 65da467f (new) + renderer.cpp 0e583c94 (base 3a970bc0) + renderer.h b60e55d8 (base
  e20cb7ed) + app.cpp d9a5cd2c (base 1a889880, ONE line) + PROGRESS.md 2a86e9a5 (base 0cc998fb). shc f 152/0.
  chain_f.sh: build f1 -> selftest -> --roomlighttest. chain_rl2 killed (superseded). Still running: chain_sp2 (spR),
  chain_sp3 (m1 selftest + rrA/rrB roofs at noon/dusk on nt_r2). Then: proofs + guarded copy + one sign-off message
  (two parts: roof rust, room light).
- 19:00 f1 selftest 0/0 (log_f1_selftest.txt); --roomlighttest on nt_f1 (main 69341af base) PASS: 5,594,400 inputs
  (1,751,066 lit) 0 diffs; 104 vectors 0/0 (rltestf_result.txt after chain_f ends). spR (rust2) still shows faint pale
  patches (crop /tmp/night/_v/sp_crops.png) -> rust3 (/tmp/night/shd_rust3/matgen.hlsl: rough 0.35..0.45, AO x0.6 on
  rust = specular occlusion) copied into tree f (matgen blob 53194ece, untested). chain_sp3 killed (m1 selftest not
  needed). chain_sp4.sh: spR3 (nt_r1 + shd_rust3, key_solano_pickets_morning) -> rrA/rr3 (nt_r2 +- shd_rust3) at
  the_flats_low_noon + key_solano_low_dusk. chain_f2.sh: build f2 (tree f with rust3) -> after chain_sp4: selftest f2.
  If rust3 is NOT better than rust2: put shd_rust2/matgen.hlsl (447646b4) back in tree f and rebuild.
  Shared tree clean at 19:00 (all target files == HEAD 69341af; new files absent).
- 19:15 chain_h.sh: tree /tmp/night/h69 (pure 69341af) -> build nt_h69 after build_f2 -> after chain_f2: hN (h69) and
  fN (f2) shots downtown_street_night + _day (1080p Q3, both in one game) = "nothing else changed" check. Timing: no
  per-frame cost (rust baked at load; roomNight one expression; HLSL roomLight not called by frame shaders yet);
  --roomlighttest ~29 s at startup on llvmpipe, only with the flag. Environment::gameDay appended at the struct's
  end; saves write env fields one by one (no layout risk).
- 19:25 spR3 (rust3): pale patches gone but DARK spots on the sky-reflecting part (-13% vs spN, p10 -24%; visible as
  dots in rows at 960 px). Measured (/tmp/night/ana/rustratio.py, rust px vs spN): spA +17/+31/+1% (left/middle/glare),
  spR +5/+18/+6%, spR3 -13/0/+5%. Linear fit -> roughness 0.35..0.45 with NO specular occlusion ~ +-4%: rust4 =
  /tmp/night/shd_rust4/matgen.hlsl (rust2 + rough 0.35..0.45; blob 3055679455), now in tree f. NOTE GEN 16 is also
  MAT_CORRUGATED (shed/warehouse walls, metal 0.7). Killed chain_sp4 (+ its rrA run), chain_f2 (+ f2 build), chain_h.
  New: chain_sp5.sh (spR4 on nt_r1 + shd_rust4); chain_f3.sh (RESUMABLE): build f3 (tree f) -> build h69 (pure main)
  -> shc tree f (shc_f3.txt) -> after sp5: selftest f3 -> hX (h69) and fX (f3) five views each (pickets morning, flats
  noon, key solano dusk, downtown night, downtown day).
- 19:35 spR4 (rust4): left -9%, middle +7%, glare +6% (vs spR +5/+18/+6, spR3 -13/0/+5): the most neutral -> CHOSEN.
  Crops /tmp/night/_v/sp_crops4.png. f3 built rc 0 (rust4) but roomlight_test.cpp then fixed (comment said 559,440 ->
  5,594,400; bool counting made explicit for MSVC): blob 11371e79. chain_f3 killed (its h69 build.sh pid 10209 left
  compiling as an orphan). chain_f4.sh (RESUMABLE): wait 10209 -> accept/rebuild h69 -> build f4 -> shc_f4.txt ->
  selftest f4 -> rltf4 (--roomlighttest, rltestf4_result.txt) -> hX (h69) / fX (f4) five views each.
- 20:07 f4 checks DONE: h69 built (orphan, rc 0), f4 rc 0, shc tree f 152/0, selftest f4 0/0 (9 pipelines, debug
  layer 0), --roomlighttest on f4 PASS (5,594,400 / 1,751,066 lit / 0 diffs; vectors 0/0). Running: hX (h69) then fX
  (f4), five views each. Then proofs (proof_rust_variants.png done), guarded copy, sign-off.
- 21:45 SIGNED OFF roof rust (rust4) + room light, base 69341af (HEAD now f26714d: none of my files changed; guard OK).
  Copied into the shared tree: matgen.hlsl 30556794 (base 3240e731), roomlight.hlsli 84ab4186 (new), roomlighttest.hlsl
  ae632c1a (new), render/roomlight_test.cpp 11371e79 (new), renderer.cpp 0e583c94 (base 3a970bc0), renderer.h b60e55d8
  (base e20cb7ed), app.cpp d9a5cd2c (base 1a889880, one line), PROGRESS.md efea0df3 (base 0cc998fb; bullet reworded after the first copy). Proofs:
  /tmp/night/proof_rust_variants.png, proof_rust_AB.png, proof_rust_crops.png, proof_nochange.png. A/B hX (nt_h69,
  pure main) vs fX (nt_f4): pickets bit-identical to spA/spR4; dusk/downtown identical outside metal roofs; flats noon
  also has edge-only TAA/run-timing differences. Nothing of mine running. Next: wait for the lead.

## 10-26 22:00 Lead: roof rust + room light ACCEPTED for snapshot 66 (all 8 blobs OK on HEAD aff17b9; rough .35-.45 OK).
## NEW ITEM: facade per-window path (7.8 next step) on TODAY'S PROCEDURAL facades (answered yes, going ahead).
- Plan: FrameCB gRoomClock (uint4: x game day, y roomMinute(timeOfDay), z roomNight(), w 0) in common.hlsli +
  FrameConstants.roomClock[4] in renderer.h + fill in Renderer::updateFrameConstants (after computeSunAndSky).
  facade.hlsli: include roomlight.hlsli; schedule class per window room from roomHash + use (homes: living/kitchen/
  bed/bath mix, stairwell bays of non-house residential RS_ALWAYS; office/industrial: RS_OFFICE + some RS_ALWAYS
  corridors, office ground-floor lobbies RS_ALWAYS; storefront RS_SHOP, cafes RS_RESTAURANT); level/254 scales the
  lamp; keep litFrac as the building's vacancy gate (calibrate vs today's lit fractions at 20:00).
- Caveat sent to the lead: city table turns all home rooms off by ~00:25 weekdays (new game = Tuesday) -> 01:00 house
  streets dark (today ~1 in 8 home windows lit). Night-owl interval would be the city's change (roomlight.h slot 3 of
  RS_HOME_LIVING free; their vectors + my HLSL table regen).
- Sign-off: A/B night shots residential street + downtown at 20:00 and 01:00, gbuffer timing, selftests; base on
  snapshot 66 when it lands (my 8 files are uncommitted in the shared tree until then).
- LATER small follow-up: roof rust world-space variation (the 2.5 m tile repeat; needs a world-space term in the
  gbuffer/material shader, outside matgen).
- 22:20 Lead: GO; ALSO add night owls to world/roomlight.h RS_HOME_LIVING slot 3 MYSELF (lead tells the city): ~1 home in
  8-10 by seed, lit until 1:30-3:00, a little more Fri/Sat nights if the table allows; regenerate vectors + property
  checks in tests/world/worldmodel_test.cpp + HLSL table, one sign-off; include the 01:00 residential A/B.
- 22:40 Tree /tmp/night/w (aff17b9 + my 8 snapshot-66 files + new work): roomlight.h owls {1320,1575,14,200,45,0}
  weekdays / chance 21 weekends (weekend = Sat+Sun nights; Friday keeps the weekday share: table limit); generator
  /tmp/night/rlwork2/gen.cpp (table.txt, vectors.txt); 104 old vectors unchanged + 4 owl vectors (108; also in
  render/roomlight_test.cpp); owl property checks; worldmodel_test built with CI flags: all passed. gRoomClock
  (common.hlsli FrameCB, renderer.h FrameConstants.roomClock, renderer.cpp fill + include roomlight.h). facade.hlsli:
  facadeRoomSchedule + shopIsCafe + roomLight-driven lit/levelK/afterHours; roomlight.hlsli class constants. shc w
  152/0. PROGRESS bullet added. Tree /tmp/night/h66 = aff17b9 + 8 files (before). chain_w.sh (RESUMABLE): build h66,
  w1 -> selftest w1 -> rlw1 (--roomlighttest + 640x360 sanity: downtown aerial/pickets at 20:00, 01:00) ->
  hW/wW 1080p Q3 six views (residential=pickets framing, downtown aerial, calle luna; 20:00 + 01:00) -> tours tH/tW
  (stops 4-8) for gbuffer timing (perstop3.py).
- 22:45 h66 + w1 built rc 0; selftest w1 0/0; --roomlighttest w1 PASS (108 vectors 0/0) but the sweep seeds (stride
  211) hold NO night owls -> stride 205 (1 weekday + 3 weekend owl seeds) in roomlighttest.hlsl + roomlight_test.cpp;
  chain_w2.sh: after the A/B shots, build w2 + shc + selftest + --roomlighttest (final exe). Sanity frames
  /tmp/night/_v/sanity_w1.png OK (downtown lit at 20:00, sprinkle at 01:00; pickets house lit at 20:00, dark at 01:00).
  chain_w3.sh: hR/wR Key Solano aerial at 20:00 + 01:00 (owls over many homes), after the A/B shots.
- 23:00 Lead: PR #59 merged, main = 41c3ca0 (snapshot 66: my 8 files as signed; app.cpp 7c65554a = my line + menu
  camera hunks). BASE the per-window work on 41c3ca0. Tree /tmp/night/w66 = archive 41c3ca0 + my 10 changed files
  (worldmodel_test.cpp re-applied on 41c3ca0's bde87a90; others' bases identical); worldmodel_test (CI flags) passed.
  chain_w2 killed; chain_w2b.sh: build w2 from w66 -> shc -> selftest -> --roomlighttest (+2 spot frames rlw2).
  A/B stays h66 vs w1 (same base, aff17b9); the final exe is w2 (41c3ca0-based).
- 23:55 A/B hW (h66) vs wW (w1): hW OOM-killed after 4 shots (calle luna lost) -> chain_w4.sh reruns them (hW2 ->
  copied to hW_calle_luna_*). Results so far: residential (pickets) 20:00 similar count (3 -> 4 lit), 01:00 house +
  neighbours dark after (no owl among these 4 homes; before had 2 lit); downtown 20:00 brighter, 01:00 brighter
  (bright px 1.23% -> 2.10%: office corridors 7% always-lit, scattered). Waiting: hR/wR key solano aerial (owls over
  many homes), hW2, w2 checks (chain_w2b), tours tH/tW (chain_w) for gbuffer timing.
- 10-27 00:50 CONTAINER RESTART 00:45 (all processes died). Done before it: build w2 rc 0 (tree w66 = 41c3ca0 +
  changes), shc w2 152/0, selftest w2 0/0, tour tH (h66) done. Missing -> chain_w5.sh (RESUMABLE): rlw2
  (--roomlighttest on w2 + 2 spot frames), hW2 Calle Luna on h66 (-> hW_calle_luna_*), hR/wR Key Solano aerial
  20:00/01:00 (shoot1 per view), tour tW (w1).
- 01:00 Lead: main = 0e4bf3a (snapshot 67: traffic AI, stairs, worldv; none of my files; my 10 bases unchanged).
  SIGN-OFF BASE 0e4bf3a. Tree /tmp/night/w67 = archive 0e4bf3a + w66's 10 files; chain_w6.sh: build w3 -> selftest
  w3 -> --roomlighttest w3 (rltestw3_result.txt). Guard at copy: the 10 files == 0e4bf3a's (same as 41c3ca0's).
- 02:55 rlw2 PASS (sweep 5,594,400, 1,739,772 lit with the owl seeds; 108 vectors 0/0). hW2 (-> hW_calle_luna_*),
  hR/wR done. Calle Luna view = a wall close-up (one window beside the camera off after; exposure shifts) -> left
  out of the proofs. Proofs: /tmp/night/proof_rooms_residential.png (pickets + Key Solano aerial, 20:00/01:00),
  proof_rooms_downtown.png, proof_rooms_crops.png (1:1 at 01:00). Waiting: tour tW (w1) vs tH, chain_w6 (w3 on
  0e4bf3a: build, selftest, --roomlighttest). Then guard (10 files == 0e4bf3a) + copy + sign-off.
- 03:10 SIGNED OFF facade per-window lighting + night owls on main 0e4bf3a (guard OK, copied): roomlight.h 1e7704ab,
  worldmodel_test.cpp 0d1aa92f, roomlight.hlsli b92c5f90, roomlighttest.hlsl 5c5713fd, roomlight_test.cpp fcfbe25d,
  common.hlsli 9bc43be8, renderer.h ee302b14, renderer.cpp 5ff3ef6b, facade.hlsli 43f8e7bb, PROGRESS.md 1511ddc9.
  nt_w3 (0e4bf3a-based): rc 0, selftest 0/0, --roomlighttest PASS (1,739,772 lit, 108 vectors). Tour gbuffer +2% raw
  / +9% norm (noisy). Nothing of mine running. Next: wait for the lead. Later follow-up: roof rust world-space term.
- 03:30 Lead: per-window ACCEPTED for snapshot 68 (worldmodel_test.cpp merged with the city's written-bulb tests by
  the lead -> rebase my test edits on 68's file at sign-off). Decisions: downtown 01:00 OK, shop display 45% OK,
  parking garages: lead asks city for a use flag later. NEXT: (1) Friday day type, (2) roof rust world-space
  variation, sign both off, then PARK with notes current (usage tight).
- 03:40 (1) Tree /tmp/night/x = 0e4bf3a + w67's 10 files + FOUR day types (RD_WEEKDAY Mon-Thu, RD_FRIDAY, RD_SATURDAY,
  RD_SUNDAY: a day's row covers its daytime and its night, so Fri = workday + late night, Sun = day off + early night;
  3 types can't express it). roomlight.h table [13][4][3], roomDayType(); HLSL kRoomIntervals[156] + roomDayType;
  vectors: 108 unchanged + 6 day-type vectors (114) in test + roomlight_test.cpp; checks owlFri==owlSat,
  owlSun==owlWeek, bars at 3:30 (Thu 280, Fri 1605, Sat 1605, Sun 280 of 2000). worldmodel_test passed. PROGRESS
  bullet updated. Generator /tmp/night/rlwork2/gen2.cpp. chain_x.sh: shc x, build x1, selftest, --roomlighttest.
- 04:00 (2) rust world-space variation: tree /tmp/night/y = tree x + materials.cpp MF_WEATHERED (1024) on
  MAT_ROOF_METAL + MAT_CORRUGATED + world.hlsl psWorld: base samples unchanged, for MF_WEATHERED two SampleGrad samples
  at offsets (x: whole ribs 1/16, y: any) picked by floor(valueNoise(world xy*0.07 + z*0.05)*5), blended
  (IQ technique 3) + PROGRESS bullet reflowed. shc y 152/0. chain_y.sh: build y1 (after x1) -> selftest y1 ->
  --roomlighttest y1 -> rust A/B rvA (x1) / rvB (y1): pickets morning, flats noon, key solano dusk.
- 04:15 Lead: main = 886dc47 (snapshot 68: my per-window files as signed; worldmodel_test.cpp = d630dd0c, merged with
  the city's written-bulb tests). Snapshot 69 adds the city's dead-end test: worldmodel_test.cpp a2bfda68 -> base my
  test edits on it (or tell the lead). Tree /tmp/night/z = 886dc47 + y's roomlight.h, roomlight.hlsli,
  roomlight_test.cpp, materials.cpp, world.hlsl, PROGRESS.md + worldmodel_test.cpp = a2bfda68 + my day-type edits
  (c695bcb4). Test vs 886dc47 code: only the city's dead-end test fails (line ~1227; it fails identically without my
  edits: needs 69's code) -> say so in the sign-off. chain_z.sh: build z1 (886dc47-based) -> shc -> selftest ->
  --roomlighttest. chain_y continues (y1 + rust A/B rvA x1 / rvB y1).
- 04:35 x1/y1 built rc 0 (y1 0 warnings), selftests 0/0, --roomlighttest x1 + y1 PASS (1,743,432 lit; 114 vectors).
  Rust variation A/B rvA (x1) / rvB (y1): lattice of dots broken on the Key Solano roof and the dusk hip roof, ribs
  aligned, no seams. Proofs /tmp/night/proof_rust_variation.png (1:1 crops) + proof_rust_variation_dusk.png.
  worldmodel_test edits on 68's d630dd0c -> 3f65b9c0 (all passed vs 886dc47 + my files; goes in the shared tree) and
  on 69's a2bfda68 -> c695bcb4 (blob written, for the lead's merge; only the city's dead-end test fails vs 68 code).
  Waiting chain_z (z1 = 886dc47 + my 6 files: build, shc, selftest, --roomlighttest) -> guard + copy 7 files ->
  sign-off -> PARK.
- 04:45 SIGNED OFF day types + rust world-space variation on main 886dc47 (guard OK, copied): roomlight.h ab24a1e4,
  roomlight.hlsli c1596d00, roomlight_test.cpp 021f974f, materials.cpp 01c3165a, world.hlsl a69791a8, PROGRESS.md
  5fce52b7, worldmodel_test.cpp 3f65b9c0 (on 68's d630dd0c; the same edits on 69's a2bfda68 = blob c695bcb4 for the
  lead's merge). nt_z1 (886dc47-based): rc 0, 0 warnings, shc 152/0, selftest 0/0, --roomlighttest PASS (114 vectors).

## PARKED 10-27 04:45 UTC (lead: usage tight; park after these sign-offs). Nothing of mine running.
Open items for later (lead will resume me):
- Parking garages should stay lit all night: needs a facade "use" flag from the city's procedural generator; then
  map it to RS_ALWAYS in facade.hlsli facadeRoomSchedule (lead will ask the city and come back).
- 7.8 remaining (hand-built world): room id MRT (R16_UINT), room table + tile room filter (4 ids/tile), froxel fog
  light filtering by room, photometric table by catalog code=, emissive fixtures, window records path (worldc u32 per
  window replacing facadeRoomSchedule), lampSched gather (city's blocksbin lampSched -> switch lamps by roomLight),
  --gfxstats counters.
- Private trees of the last work: /tmp/night/z (final, 886dc47-based), /tmp/night/x, /tmp/night/y; generators
  /tmp/night/rlwork2/gen.cpp, gen2.cpp (table + vectors from roomlight.h).
