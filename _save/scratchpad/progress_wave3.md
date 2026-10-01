## Integrated 2026-09-30 morning (wave 3)
- First-person gun handling (src/game/fpweapon.cpp, Anim::holdGrip): in first person every gun sits in front of the
  eyes, low right at the hip, raised onto the line of sight while aiming (iron sights centred; the rifle's open
  red-dot tube and the SMG reflex window show a HUD red dot), tipped across the body while sprinting, rolled over
  while reloading (the support hand lets go to fetch the magazine), pulled in and tipped up against walls; it lags
  quick turns, bobs with the stride and kicks back per shot. After the camera update both hands are IK'd onto the
  weapon's grips (fist centre, handle axis and palm per gun; vertical foregrips move the support hand). Magnifying
  scopes (sniper, scoped rifle / revolver) switch to a scope sight picture on the HUD (duplex or mil-dot reticle)
  with the gun hidden; aiming the sniper rifle always looks through its scope. Shots and tracers leave the visible
  muzzle. `--autoplay fpguns --firstperson` screenshots every gun and pose.
- Cloth: mip-stable fabric textures and garment-UV yarn relief (no moire); garments choose their weave (woven shirts,
  suits, uniforms, blouses, skirts and dresses; rib-knit tanks; twill trousers, shorts and work wear; jersey tees,
  polos and hoodies; denim keeps its own twill).
- Street life: owners walk round to cars parked at the curb, get in, signal and pull out at a gap; passing cars
  pull into free curb spots and the driver walks into a nearby building; lane-change conflicts, stop-line roll-up and
  repeatedly stuck cars are resolved.
- Transit end-to-end tests pass by day: bus (route 9, stops, skip, alight), SkyLine metro (Civic Center to Canvas
  District), ferry (to Port Isle and ashore); station fare gates with two lanes, platform strip lights by day,
  varied waiting crowds; the Sol Beach streetcar loop (6.24 km, 16 stops, 4 trams) is being brought up.
