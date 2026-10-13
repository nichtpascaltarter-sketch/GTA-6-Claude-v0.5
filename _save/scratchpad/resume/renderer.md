# Renderer agent resume notes (updated 2026-10-06 01:10 UTC; container restarted 00:57, chain relaunched)

## Done
- Phase 1 (GPU-driven props): merged in 06580c3.
- Phase 2 (GPU-placed decor plants): SIGNED OFF 22:12 ("renderer snapshot-safe"), copied into main's working tree
  (guarded, on 8ee5699). Blobs: scratchpad/renderer_p2_blobs.txt. Tree /tmp/fx/gd2 (06580c3 + phase 2).
  Proofs: /tmp/fx/proof_p2_grove.png, proof_p2_topdown.png; timing logs log_p1t.txt + log_p1t2.txt (base) vs log_p2t.txt;
  /tmp/fx/perstop2.py A B; self-test log_p2_selftest.txt (19 PASS 1 SKIP).

## In progress
- Phase 3 (palms: palmFrondStrip, trunk rings MAT_BARK param 2/3, coconuts; MAT_LEAVES leaf canopy GEN 28 in
  matgen.hlsl + materials.cpp; materials.cpp gens[] array overflow fixed (kGenerators 32); storefront glass in
  facade.hlsli for the city agent). Tree /tmp/fx/gd3 (= gd2 + phase 3). Exe bin/nt_p3c.exe (built before the final
  propcull: runs use --shaderdir Z:\tmp\fx\shd_p2 for propcull only).
  Files: src/world/propmesh.cpp src/shaders/props.hlsl src/shaders/matgen.hlsl src/render/materials.cpp
  src/shaders/facade.hlsli PROGRESS.md. Integration: DRY=1 sh /tmp/fx/integrate_p3.sh then sh /tmp/fx/integrate_p3.sh.
  Verified: 1080p stops 11-13 (p3h2 vs p2h4: palms differ, hedges/shrubs leafy), stop 3 storefront (p2s3 vs p3s3).
  Pending: close-ups ps2/ps3 (palm crown, hedge), timing p3t vs p2t (stops 4-13), self-test with nt_p3c, shc.
  Then sign off; tell the city agent (aa6f7b18ee2c9d06f) when the storefront glass lands (they drop their stopgap).
- Character backlog (lead): 1 lamp feature-shadow bounce fill (kFeatureBounce 0.1), 2 per-card dither threshold in
  psHairCard, 3 feature shadows for lash/brow/beard cards (G-buffer extra bit 3), 5 hair shell wrap 0.15 (cards 0.4).
  Tree /tmp/fx/gd4 (= gd3 + these, lighting.hlsl + dynamic.hlsl), override dir /tmp/fx/shd_c1. Facecams c1a (before)
  / c1b (after) in /tmp/fx/fc with /tmp/fx/fc_c1.args (faces agent layout). Sign off separately after phase 3; tell
  the faces agent (a80b00be5132548a0) when it lands.

## Chain
- /tmp/fx/chain_v2l.sh (detached): c1a, c1b, ps2, ps3, p3t. Markers: /tmp/fx/fc/c1a.done, c1b.done, /tmp/fx/ps.done,
  /tmp/fx/p3t.done, /tmp/fx/chain_v2l.done.
- Rules: games only via tools/run.sh on slot 4 (NT_D3D12_SLOT=1, WINEPREFIX=/tmp/wine_fx), one at a time; builds via
  build.sh (QUICK=1); memfree gate; never pkill -f with a pattern that matches the calling shell.

## Update 01:10
- Phase 2 merged (PR #31, main 6afc8b2); main now 8fb1e66 (city batch 5), src/render untouched.
- Phase 3 timing p3t (nt_p3c) vs p2t: prop triangles +25-49%, normalised shadows +7% / gbuffer +4% summed -> palms
  trimmed (frond strips 9 segments, coconuts 6x4), leaf canopy tuned (less sheen / gaps / yellow); exe nt_p3e.
- Character item 1 now also has a lamp ground-bounce fill (kLampBounce 0.2, people within 12 m, normals facing down);
  the faces agent says the sockets are dark because they face away from the lamp (not the feature shadow).
- Chain /tmp/fx/chain_v4.sh: self-test nt_p3e, close-ups pt2/pt3, facecams c2a/c2b (main 111efa9 exe nt_m111 vs
  --shaderdir shd_c2), then p3t2 timing (nt_p3e) vs p2t. Markers p3_selftest.done, pt.done, fc/c2a.done, fc/c2b.done,
  p3t2.done, chain_v4.done.

## Update 04:55
- Phase 3 SIGNED OFF (copied into main's working tree on d3c2c3f; blobs scratchpad/renderer_p3_blobs.txt; proof
  /tmp/fx/proof_p3.png; exe nt_p3e; timing p3t2 (stops 4-10) + p3t3 (11-13) vs p2t). Waiting for the lead to merge.
- Character sign-off READY (not yet copied): once phase 3 is in main (PROGRESS.md == /tmp/fx/gd3/PROGRESS.md), run
  DRY=1 sh /tmp/fx/integrate_c1.sh then sh /tmp/fx/integrate_c1.sh; proof /tmp/fx/proof_char.png; facecams c2a/c2b in
  /tmp/fx/fc (main 111efa9 exe nt_m111 vs --shaderdir shd_c2). Measured c27 0.5 m night: sockets 5.9 -> 4.2 stops below
  the forehead. Then tell the faces agent (a80b00be5132548a0): re-check night portraits; brows now accumulate (snapshot
  33's darker brows may now read too dark); hair shell wrap 0.15. Tell the city agent (aa6f7b18ee2c9d06f) when the
  storefront glass (phase 3) merges, so they drop their 45% stopgap.
