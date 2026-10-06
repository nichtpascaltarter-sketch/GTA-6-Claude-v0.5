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
