Characters realism pass 3 (lead brief). You built the character system earlier (src/anim); welcome back. Since then:
the renderer agent fixed cloth moiré (mip-stable fabric texture + garment-UV yarn relief) and made MAT_CLOTH weaves
selectable with (mat >> 8) & 3 (0 jersey, 1 plain weave, 2 twill, 3 rib); the lead wired GarmentDef::matParam
(woven shirts / suits / uniforms / blouses / skirts, rib tanks, twill trousers and shorts). Hair, brows and lashes are
strand cards; faces were sculpted from anatomical primitives (d892b64, 80c9aff). The lead added Anim::holdGrip
(pose.cpp) for first-person gun holds; please keep its conventions (handGrip frame, kGripAlong / kGripPalm).

Goal: close the biggest visual gap vs GTA 6 at close range: faces, skin and hands. Current state at 2-3 m
(/tmp/fx/cl1_c6_chest.png, /tmp/fpw/auto_fpguns_02_pistol_sprint.png from the earlier run): waxy uniform skin, small
deep-set eyes, ears standing out from the head, flat lips, hands that read as mittens in first person.

Work items, in order of visual payoff:
1. Face proportions and features per sex / age / ancestry: eye aperture size and lid shapes (upper lid crease,
   lower lid thickness), brow ridge, nose (bridge, alar wings, nostrils), lips (vermilion border, philtrum, corners),
   ears closer to the skull with a readable helix / antihelix, jaw and cheek variety. Compare against real proportion
   references you know (eyes at mid-height of the head, eye width ~1/5 of head width, etc.).
2. Skin albedo variation in the vertex colours / skin data the shader reads: redness at cheeks, nose, ears and
   lips, darker periorbital area, stubble shadow on shaved male faces, freckles / moles / blemishes by seed, age
   wrinkles (forehead, crow's feet, nasolabial) as geometry or normal detail. If the skin shader (SM_SKIN,
   dynamic.hlsl / lighting.hlsl) needs a new parameter (pores, specular map, SSS tint), ask the renderer agent
   (afef9ce4809b4c421) with a precise spec; do not edit its files yourself.
3. Hands at LOD0: separated fingers with knuckles and nails, a real palm; they fill the frame in first person when
   holding guns (holdGrip curls B_FINGERS / B_THUMB as single bones: keep the fist shape convincing).
4. Eyes: cornea bulge / wet line, iris detail, lashes on both lids; teeth and inner mouth when talking.
5. Keep LOD budgets (LOD0 ~21-30k tris, LOD1 ~4.5k, LOD2 ~1.5k) and build time per character.

Verify with the character viewer (--viewer characters, --shot close-ups at 0.6 m, 1.5 m, 3 m) and a crowd shot,
by day and at night, and a first-person gun shot (--play --firstperson --autoplay fpguns). Send the lead a milestone
report with image paths after each item. Rules: gate every build / Wine run with the scratchpad memgate.sh (full build
~1.6-2.6 GB, Wine ~1.5 GB; the machine has 4 cores shared by 6 agents), use your own WINEPREFIX and stdout files, leave
changes uncommitted (the lead snapshot-commits build-verified trees), no placeholders / stubs / TODOs, original IP only.
