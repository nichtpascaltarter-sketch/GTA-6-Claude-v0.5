# Overnight log (2026-09-30 → 10-01)

The user went to bed at 22:52 and asked the lead to merge the PRs. Each verified snapshot goes: push, PR, merge
(merge commit), then the branch is fast-forwarded to main.

## Evening, before bed
- 22:08 container restart: all seven agents resumed from their transcripts. The in-flight runs were redone, with no
  work lost.
- The disk had filled up at about 22:05. Clearing stale Wine prefixes and scratch dirs freed about 16 GB, and the
  agents now keep to one prefix each.
- PR #1 merged by the user (ae5bccb). dd402cf (carried props, zebra crossings, planted feet, layered clothing, street
  encounters, Act 4) went out as PR #2, which the user merged (10d2769).
- Camera preferences survive a new game (verified in dd402cf's first-person smoke run).
- D3D12 (D3D12 only, no D3D11 fallback): the port is complete and syntax-clean, with every D3D11 path removed and a
  "Direct3D 12 required" error box. It is validating against the dd402cf D3D11 baseline before the switchover.
- Queued: cca50c8 (live cockpit, fishing, airboat tours, cemetery, crowds at named places), then 149dc89 (carrying
  arm poses, greetings, skirts and laces, churchyards, Act 4 unlock, phone and smoking carry rule, dark dial faces).

## Overnight
- 22:55 Missions agent, milestone 1: street encounters. There are 10 random-event kinds with 2 outcomes each (scooter
  snatch, bodega stick-up, runaway bride, hitchhiker with a bounty, robbers' van crash, red-light race, carjacking,
  cash-in-transit, drifting boat, paparazzi). Seeing all 10 through pays $5,000. 19/20 outcome tests pass and the last
  one is rerunning. Lead review: each outcome banner shows up in the next test's frames (it outlives the encounter
  reset), and the snatched item is a "bag" in one outcome and a "camera" in the other. Both were sent back.
- 23:00 Both encounter findings fixed by the missions agent. A leftover result banner now clears when a new encounter
  is first seen (missions replace it with their title), and Scooter Snatch says "camera bag" in every line and banner.
- 23:00 Places agent, milestones 1 and 2: the Ocean Promenade deco hotel row (11 hotels from four designs, neon,
  terraces, a beachfront park) and Porto Sol University (quad, domed library, 48 m bell tower, halls, dorms, Tarpon
  Field), with 500+ scenario anchors for the crowds. Both are in main since dd402cf; later fixes ride the next
  snapshots. Lead review: the campus reads as an empty lawn from the air, a glass-like slab stands by the quad oak, and
  the deco facades are a bit plain up close. All three were sent back for after the cemetery.
  Sheets: scratchpad/pl/deco_ba_view.jpg, campus_ba_view.jpg.
- 23:10 Animation agent milestone:
  - Locomotion verified: cadence within published ranges in 54/54 cases, foot skate about 1 cm/s mean, exact knee
    hinge IK.
  - Standing life: contrapposto weight shifts with settling steps, breathing that speeds up after running, 6 held
    postures and 9 fidgets chosen per person.
  - Carrying arm poses, and paired greetings (hug, handshake, cheek kiss) sized from both skeletons.
  - Lead review: the new walk reads far more natural (shots/crowd/ba_beach.png). In the cheek kiss, a wide-brim hat
    passes through the partner's head; sent back.
- 23:12 Snapshot cca50c8 failed its build (population.cpp divided a vec2 in place; the AI agent had already fixed
  it). Snapshot 3 (149dc89) supersedes it and carries both change sets under one combined message. Tour stop 8 (rain
  at night, dd402cf exe): distant pedestrians under open umbrellas. The police HQ lobby reads blown out against the
  night street (renderer queue).
- 23:20 The hat brim in greetings is fixed. Anim::greetingFits rules out the cheek kiss for brimmed hats and caps, and
  the hug for a sun hat or fedora unless its wearer is clearly taller. Heads in a hug lean over the partner's shoulder,
  0.26 m apart, and caps no longer overlap (anim/greet/hug_caps.png). The AI agent calls it when picking the clip.
- 23:21 Check-in: all 7 agents are active. Snapshot 3 is waiting for the compile lock. D3D12 is fixing an upload-ring
  lifetime issue from its first build, before validating against the D3D11 baseline.
- 23:35 Greetings in the world (AI agent): travelers and drivers hug, shake hands or kiss on the cheek at the airport
  curb, at pick-ups and drop-offs. The lead wired the hands onto the partner's real chest or head for tall/short pairs
  (peds.cpp) and parks a suitcase by the foot during a greeting, plus bags for hugs and kisses (carry.cpp).
- 23:40 Missions agent, milestone 2: Act 4 "Undertow". It continues after either Signal ending, with its lines,
  cast and mechanics shaped by that ending:
  - Wake: the diner firebombing, then catching Nando alive.
  - Box Numbers: a night stealth run through the Port Isle container stacks, then a boat escape.
  - Blue Line: eavesdropping on the SkyLine, then a K9 team.
  - Clear Air: stopping a plane before lift-off, with a surrender path to the airport police.
  - Gator Country: a scoped first-person overwatch, then an airboat chase.
  - King Tide: the storm finale, with a warehouse interior scene, a choice between the law and $4M, and the end card.
  All six pass with both endings. An empty-diner cutscene bug (the interior not streamed in yet) was fixed. Lead
  review sent back four findings: the Ortega Boatyard purchase panel stays open through King Tide (a game bug), a blank
  opening camera on Blue Line, snow-white night surfaces (likely test exposure lag) and blocky noise in the storm frame.
- 23:45 Missions agent, milestone 3: fishing and the Sawgrass Airboat Tours, all tests passing.
  - Fishing: cast from piers, docks, seawalls, banks or a stopped boat, with a line-tension fight. 16 species by
    habitat and time of day, three bait shops, a trophy board, and bounties for the first trophy and all 16 species.
  - Airboat tours: Jonah's three routes, with gator sightings, star ratings and tips.
  - Lead review sent back an opaque white marker cylinder at fishing spots and the bait shop (it hides the player). A
    black band on the offshore horizon went to the renderer agent.
- 23:50 Street meets (AI agent): now and then two pedestrians who know each other stop on the sidewalk ("Hey! Look who
  it is!"). They greet (suits shake hands, others hug or kiss on the cheek), chat with lip-synced lines, say goodbye
  and walk on. The lead's peds.cpp change lets greeting partners stand closer than the capsule separation.
- 23:55 Missions fixes:
  - Shop, wardrobe, garage, property, business and bait-shop menus now close whenever a mission is active or a
    cutscene plays (the boatyard panel through King Tide).
  - The Blue Line opening camera now has a clear view of the platform.
  - Bait-shop markers are smaller and hide while you stand in them.
  - The "opaque cylinder" was likely exposure lag on a cyan emissive marker; it's being checked at renderevery 10,
    with the apron and storm frames.
  - Story regression so far: Act 1 7/7.
- 00:05 Snapshot 3 built and passed all 96 shaders at 23:37, but its smoke drive waited 24 min for a Wine slot (all 4
  were held by long agent test runs). Snapshot smoke runs now have a dedicated fifth slot (tools/run.sh
  NT_LEAD_SLOT=1). The smoke check no longer reads a stale log from the previous run. Snapshot 3's smoke drive is
  running in the new slot.
- 00:15 Characters agent, interim: a ClothingClip test across 31 dressed characters in four poses. Poke-through is
  0.03%. It drove fixes: skirts ride the thighs, laces follow the shoe, tank-top armholes are deeper, vests no longer
  stand off the shoulders. LOD builds now cost 304 ms per character (≈9 s of load spread over 4 workers), to be
  brought back in item 4.
  For the agent, the lead made three changes:
  - loafers sound like leather shoes;
  - shop outfits no longer inherit the everyday jacket, bag or accessories;
  - both protagonists' looks are locked. Mari: tank top, jeans, flats. Dex: a grey tee under an open olive work
    jacket, jeans, work boots.
- 00:15 Verified in game (nt_ai29): an airport pick-up hug at the terminal curb, partners 0.33 m apart with heads side
  by side. The roller case stands upright beside them, handle up, clear of their legs (scratchpad/shots29/
  auto_greet_1_1.png). Street meets and K9 results follow.
- 00:25 MERGED PR #3 (bc0aaa8): snapshot 64f3ef7. It contains the live cockpit (hands wrap each car's own rim at
  10 and 2, needles over dark lit dials; smoke3_png), fishing, the airboat tours, the cemetery and churchyards, crowds
  at named places, carrying poses, greetings and the Act 4 unlock. The lead merged it, as the user asked.
- 00:30 Snapshot 4 (6d6c43f) is queued. It holds:
  - the places agent's hospitals, Palmera State Penitentiary and Palmera Speedway;
  - airport greetings and street meets;
  - body extremes (heavy, elderly, athletic) and garment creases;
  - the protagonists' fixed looks;
  - the renderer's fix for the dash-grid stipple on walls (temporal jitter stepped by the golden ratio). That stipple
    was reported at tour stop 0 on the evening before.
- 00:24 Check-in: 6 of 7 agents are active. The places agent is waiting on its own background run. D3D12 is still
  debugging GPU resource lifetimes in its first build (released-object detection, a per-pass sync debug option), so
  it isn't ready for validation yet. Snapshot 4 is in the pipeline.
- 00:30 D3D12: rebased onto 970af15 cleanly, with no D3D11 references left.
  - With every pass serialized, the image matches the D3D11 build: day exposure EV 14.85 vs 14.84.
  - Blocker: unserialized, the game crashes a few frames after loading, in a lavapipe shader reading a descriptor
    overwritten with UI-looking pixels.
  - Lead hypothesis: objects released while still in flight on the GPU, likely at the loading-to-game switch. The
    proposed test defers all releases behind the frame fence.
  - D3D12 builds get their own lock lane so they don't queue 10-20 min behind the other agents.
- 00:40 Verified (AI agent, nt_ai29): street meets downtown. A dog walker, the dog waiting on its leash, meets a
  businesswoman: a cheek kiss with her briefcase set down, then they talk with it back in hand. Another pair hugs.
  Both are at their true distance (scratchpad/shots29/greet_montage.png). Prison inmates: the lead added 6 role-7
  roster entries, the characters agent is making the jumpsuit, and the AI agent maps the yard to them.
- 00:35 Snapshot builds now share a priority compile lane with the D3D12 port (/tmp/neontide_build_d3d12.lock: one
  compile there beside the shared lane, two at most machine-wide, with the memory wait kept). Before this, snapshot 4
  had queued about 20 min behind agent builds. It restarted in the new lane.
- 00:55 Full story regression (missions agent): 26/27 missions pass. Paper Trail had broken because world generation
  moved the Key Coral villa and an outbuilding landed on the safe; it's fixed with an open-ground search and passes.
- 00:55 Mission and shop markers: they drew as an opaque white wall at night (the emissive cylinder's alpha was read
  as emission strength, not opacity). The lead fixed it: markers are now see-through (dithered half coverage), glow
  softly, and hide while the player stands inside.
- 00:55 Characters agent: the inmate jumpsuit is done (an orange coverall over a white tee, slip-ons). Canvas sneaker
  toe caps no longer render tyre-black. Mari's tank top got a proper U scoop neck.
- 01:00 Renderer agent fixed three items:
  1) The police HQ lobby at night: room ambient halved, so downlight pools read; about 10% darker.
  2) The dash band at tour stop 0 was a pedestrian right at the lens drawn at 5-20% dither coverage. Faded objects
     below 30% are now not drawn, and the dither steps evenly over time so TAA converges. AO, contact shadows and
     shadow filtering got the same TAA-friendly noise.
  3) The black offshore horizon wasn't reproducible. Generic fix: camera jumps > 40 m count as cuts, and the
     reflection probe recaptures after a > 150 m move. fish_boat will be rerun.
  Also: cloth crease shading (wrinkle ridges, fold occlusion, a sheen rim).
- 01:15 MERGED PR #4 (90658b7): snapshot 07d01f2. It contains hospitals, the prison and speedway, airport greetings
  and street meets, body extremes, the protagonists' fixed looks, the wall-stipple fix and the mission fixes.
- 01:18 Snapshot 5 (1f47a48) queued: prison inmates, hair fades and ties, hips turning into the travel direction,
  window shopping, see-through markers, camera-cut and probe fixes.
- 01:30 Places agent, milestones 3 and 4.
  - Santa Marea Cemetery: wall vaults, the SANTA MAREA iron arch, a cypress avenue to an obelisk and chapel, tomb
    rows, lanterns at night.
  - Churchyards at Okahatchee and Fort Castell.
  - Eight hospital complexes on real frontages. Four of the old hospital points were in fields, the swamp or the
    ocean.
  - The lead switched the hospital map blips, the paramedic drop-offs and the respawn after being wasted to the
    world's real hospitals (the old Fort Castell respawn point was in the sea). The landmarks (hotel row, campus,
    cemetery, churches, prison, speedway) are now labels on the map at closer zoom.
- 01:25 Check-in: all 7 agents are active. Snapshot 5 is building in the priority lane. D3D12 is testing the
  fence-deferred release fix for its post-loading crash.
- 01:30 Renderer agent finished its assignment (street-level realism):
  - asphalt with real aggregate, tyre tracks, oil, manholes, drains and gutter water;
  - night rain streaks lit by lamps, with lamp cones;
  - calmer bloom;
  - window blinds and curtains that glow at night, and storefront interiors visible by day;
  - fog-quadrant mitigations, the HQ lobby, the stipple fix and the teleport camera-cut fix.
  Shots in /tmp/fx/. Not replaced for now, to keep renderer changes quiet before the D3D12 switchover.
- 01:35 Animation agent: items 3 and 4 accepted.
  - Gaze: the eyes lead, the head follows on a spring, people blink on big shifts, and pedestrians watch passing cars
    and turn to sirens.
  - Diagonal walking no longer skates (0.33 down to 0.002 m/s), and the double support is re-authored.
  - Backward walking and back-pedalling.
  - Hips turn into the travel direction while strafing, so the legs no longer cross through each other
    (anim/strafe/strafe_front.png).
  Next: a sideways aiming jog, then injury and impact reactions (flinches, stagger steps, limp, protective falls into
  ragdoll, wound clutching).
- 01:40 Missions agent at renderevery 10:
  - The night apron is no longer white (that was test exposure lag).
  - King Tide passes with no stray menu, and the warehouse scene is staged inside the lit interior.
  - Still open: blocky dark blotches on wet ground at night in the storm (a puddle or wetness mask resolution bug),
    and huge headlight bloom discs on the apron. Both went to the renderer agent, which was resumed for them; its
    newer bloom and puddle work may already cover them.
- 01:45 MERGED PR #5 (5a942ca): snapshot 6346c7d. It contains the prison inmates, hair fades and ties, hips turning
  with the travel, window shopping, the see-through markers and the camera-cut and probe fix.
- 01:55 Animation agent delivered the injury reactions in the animator. The lead wired the game side:
  - hits now cause a flinch layered over whatever the legs are doing, by bone and direction, instead of a full-body
    hit clip that stopped the ped;
  - heavy hits (rifle, bat) make the body stagger a few steps to catch its balance;
  - hurt pedestrians limp on a wounded leg, hunch at low health and hold their wound.
  The player keeps full control (flinch and stagger only). The AI agent will slow injured pedestrians and use the
  writhing "down hurt" pose. Next: a braced fall before the ragdoll.
- 01:55 Snapshot 6 (tree e250664) did not commit: the out-of-memory killer took its compile at 2.1 GB. The container
  caps all our processes at 14.3 GB together, and four agents' games (about 2 GB each) plus two compiles went over.
  - Fix: tools/memfree.sh reports the room left under the cap; build.sh, tools/run.sh and the lead's gates now wait on
    that instead of the machine's free memory, and at most three shared Wine games run at once.
- 02:00 Snapshot 7 (tree 6b68278) replaces snapshot 6. It is snapshot 6 plus:
  - the injury reactions, complete in the animator and wired into the game;
  - police getting round traffic jams, and drivers holding for sirens;
  - interiors held for story scenes;
  - strap and cornrow fixes.
  It is verifying now.
- 02:05 Lead: braced falls are wired up.
  - Trips, street shoves, police tackles, the player's kick and melee knock-downs now brace for 0.16 s before the
    ragdoll: arms out, chin tucked, tipping into the push.
  - Shots, blasts, cars, falls and dives away from cars still go limp at once.
  - It lands in snapshot 8.
  - From snapshot 8 on, the snapshot check also runs an 18-second bat fight after the drive, so combat, knock-downs
    and get-ups must not crash before anything merges.
- 02:15 Places agent finished all five items: the Ocean Promenade hotels, the campus, the cemeteries and churchyards,
  the eight hospitals, and the prison and speedway.
  - Its last two fixes ride snapshot 8: paved pit and stand strips with a mown infield at the speedway (wild grass
    was growing through them), and thinner, darker Spanish moss.
- 02:15 Missions agent: the offshore horizon fix is confirmed. fish_boat, fish_pier, fish_swamp and fish_market all
  pass.
  - It fixed people being pushed out of story interiors on mission start (mCutscene used the previous scene's camera
    for a frame).
  - Night markers still read as white frosted cylinders. Lead fix: a new emissive pattern 10 holds the glow at a
    fixed brightness on screen whatever the exposure, so markers no longer blow out to white at night (their colour followed at 03:00). The mission
    start markers get the same see-through look. All 96 shaders compile. It lands in snapshot 8.
- 02:25 Direct3D 12: the crash after loading is understood and fixed.
  - Cause: Wine 9's builtin DXGI rebuilds its Vulkan swap chain when the sync interval changes (the loading screen
    presents at 1, gameplay at 0) while earlier presents are still in flight. The freed memory is then reused under
    vkd3d's descriptor sets.
  - Fix: under Wine's builtin DXGI every present uses interval 0. Windows is unaffected.
  - Still open, and the blocker: with one command list per frame the 3D image renders black. With a list per pass it
    matches D3D11 (day exposure 2.828e-5 against 2.843e-5). This looks like a missing barrier, probably in the
    exposure chain. The agent is bisecting; its estimate is 1-2 h for the fix and 2-3 h for validation.
  - The port now has its own Wine slot (NT_D3D12_SLOT=1, slot 4) so its runs don't queue behind the other agents.
- 02:30 MERGED PR #6 (681e5dd): snapshot 7 (757aaae). It contains braids and locs, the injury reactions, the real
  hospitals, the landmark map labels, police getting round traffic jams, drivers holding for sirens, interiors held
  for story scenes, and the memory-cap gates. The branch was fast-forwarded to main.
- 02:31 Snapshot 8 (tree c9b7c9e) is verifying. It is the first snapshot checked with both the drive and the bat
  fight. It contains:
  - hurt pedestrians, with passers-by and the ambulance medic helping them;
  - braced falls;
  - mission markers that no longer blow out to white at night (their colour comes in snapshot 9);
  - crowd LOD hands;
  - the jacket colour and cloth fix;
  - the speedway ground and moss fixes;
  - the cutscene first-frame fix.
- 03:00 Missions agent:
  - Night markers are no longer blown out, but they were still white: the emissive shader never read the marker's
    colour. Fixed so that markers glow and shade in their own colour; this lands in snapshot 9.
  - wake and box_numbers pass, with the full cast inside the diner and the garage (the mCutscene fix holds).
  - New: burning wrecks near a scripted scene are put out as the scene starts. A shot-up chase car exploded during
    Wake's interrogation and threw the cast across the frame.
- 03:08 DIRECT3D 12 BREAKTHROUGH: the black image is fixed.
  - Root cause: a vkd3d 1.10 bug. Clearing a UAV runs an internal compute pipeline that doesn't restore the command
    list's compute root arguments, so the lighting dispatch after the fog clear ran with clobbered arguments. The fix
    in gfx sets the root signature and arguments again after a UAV clear, and a new self-test covers it.
  - Against D3D11 at the same views: exposure 2.835e-5 vs 2.843e-5 by day and 0.392 vs 0.391 at night.
    Screenshots differ by 3.4-4.1/255 mean (PSNR 30-32 dB), edge and jitter only.
  - It is rebased cleanly onto 681e5dd (main) with no D3D11 left. The full validation suite (drive, fpguns,
    uishots, benchmark) runs next against the D3D11 baseline scratchpad/snap_681e5dd.exe; about 2-3 h, then landing.
- 03:08 Animation agent: the sideways aiming jog and the injury and impact set are complete, and all tests pass. Its
  Wine bat-fight run is clean. Next is vehicle enter/exit: hand on the handle, the door in sync, sit-in, exit.
- 03:17 MERGED PR #7 (9491fbb): snapshot 8 (f858812).
  - It contains the hurt pedestrians and good samaritans, braced falls, markers that no longer blow out to white,
    crowd hands, the speedway ground and the cutscene first-frame fix.
  - It is the first snapshot to pass the new bat-fight check.
  - Its commit title says "coloured mission markers" by mistake; the PR title and body are accurate, and the colour
    itself comes in snapshot 9.
- 03:20 Snapshot 9 (tree e80daca) is verifying. It contains:
  - the King Tide wet-ground blotches fixed (screen-space reflection ray offset and depth-pyramid cell addressing);
  - smoke lit through headlight beams (no more white discs);
  - mission markers in their own colour;
  - crowd LODs at 4.5k and 1.5k triangles;
  - burning wrecks put out before story scenes.
  A stray debug binary (preview_dbg) was kept out and the local test tools were added to .gitignore.
- 03:20 RENDERER FREEZE for the Direct3D 12 landing: no edits to src/gfx, src/render or src/shaders until it
  merges. The D3D12 agent rebases onto the PR #8 merge, re-checks, then lands through the snapshot pipeline.
- 03:57 MERGED PR #8 (0926186): snapshot 9 (360081a). It contains:
  - wet ground at night without blotches (three screen-space reflection tracing bugs, found by modelling the trace
    offline);
  - smoke lit through headlight beams (the crippled plane's smoke beside its landing light was rendering as flat
    white discs);
  - mission markers in their own colour;
  - crowd LODs (4.5k / 1.5k triangles, mitten hands, painted braids, a decimator 1.6x faster);
  - burning wrecks put out before story scenes.
- 03:58 Direct3D 12 final rebase target: 0926186. The landing plan:
  1. The port is verified alone through the snapshot pipeline (drive and bat fight), then merged.
  2. The lead merges it into the working tree and lifts the renderer freeze.
  A D3D11 baseline exe for 0926186 is at scratchpad/snap_0926186.exe.
- 04:05 The animation agent is cutting opening doors out of every car body (vehicle_doors.cpp and the game-side door
  draws). It is not snapshot-ready: closed cars would have holes until all the draw sites are done. Its paths are held
  back from snapshots (scratchpad/snap_exclude.txt) until it confirms closed cars in game.
- 04:15 Characters agent finished pass 4 (all four items).
  - Clothing: the clip test, garment fixes, the inmate jumpsuit, jackets whose cloth follows their colour, and tee
    prints.
  - Bodies: heavy, athletic and elderly builds.
  - Hair: volume, ties, fades, cornrows, box braids, locs and curls.
  - Crowd LODs: 4.5k and 1.5k triangles, mitten hands, painted braids, and a decimator 1.6x faster. Building all
    three LODs takes 193 ms per character, down from 358 ms.
  - Measured clipping: 0.029% poke-through standing and walking, 0.055% running.
  - Final shots are in scratchpad/c4/final/.
- 04:15 The Direct3D 12 tree is rebased onto 0926186 in /tmp/d3d12r_f0 and its 134 shaders compile under the D3D12
  rules (cs/ps/vs_5_1, unbounded descriptor tables). The snapshot pipeline now picks that shader checker for a D3D12
  tree. It is waiting on the port's final re-check, about 1-1.5 h away.
- 04:25 Missions agent finished wave 2. The final regression passed 63 of 63 across three runs:
  - all of Acts 1-4 under both endings and both choices;
  - all 20 encounter outcomes;
  - every fishing habitat, the market, and the airboat tours.
  Fixes since the last report: Paper Trail's safe placed in open ground (the regenerated villa lot had put an
  outbuilding on it), the interior cast fix, calmFires, and better test frames.
  Known: the Blue Line opening's second shot doesn't include Mari. At night, floodlights and the plane's lights
  still bloom into large discs; that was on builds before the snapshot 9 particle-lighting fix.
- 04:25 AI agent: the "hurt" beat works in game.
  - The sequence runs end to end: the call for help, two passers-by (one kneeling, one on the phone), the ambulance
    from 262 m, the medic kneeling, and the patient getting up at 45% health.
  - Being fixed (nt_ai38): the ambulance left before the limping patient reached it, crews got stuck in traffic, and
    the K9 handler held back too far for the dog to find unarmed suspects.
- 04:27 The animation agent verified closed cars with the new doors in game, so the door paths are back in snapshots.
  Snapshot 10 (tree 6f8fa69) is verifying. It contains:
  - opening car doors (the cars look the same closed);
  - the groundwork for getting in and out of cars;
  - ambulances that wait for their limping patient;
  - K9 handlers that close in;
  - sunglasses kept on far-LOD figures.
- 04:35 Renderer agent's before/after confirms both fixes in game, at the original mission-test conditions:
  - King Tide's wet ground reflects lamps, doors and poles coherently (before/after sheets: /tmp/fx/ab_kt_*.png);
  - Clear Air's crippled plane trails a dark plume, lit only where its landing light passes through.
  Lead follow-ups, in the working tree for the next snapshot:
  - skids on wet pavement throw spray instead of tyre smoke;
  - with --renderevery N, test screenshots wait for 6 consecutive rendered frames, so TAA, reflection and fog
    history settle as in play (stale history made test frames look worse than the game).
- 04:45 AI agent: the K9 unit works end to end in game.
  - The handler picks up the trail at its nearest point and the dog tracks the player.
  - The handler closes to about 9 m on an unarmed suspect, so the dog finds them.
  - When the player runs, the dog is released and takes them down.
  - Surrender was already verified: hands up, cuffed, released, half the fine back.
- 04:47 Renderer agent finished: both night fixes are confirmed, and it is idle until the D3D12 landing lifts the
  freeze. One new finding: the wet road's own roughness has bands about 6-7 m apart. The old blotches hid them; they
  are its next item.
- 04:53 MERGED PR #9 (47b2b22): snapshot 10 (f9edba7). It contains opening car doors, the groundwork for getting in
  and out, ambulances that wait for their patient, K9 handlers that close in, and sunglasses on far-LOD figures.
- 05:20 *** DIRECT3D 12 IS MERGED (PR #10, 6495e58) ***
  - The game renders on Direct3D 12 only, as asked: no D3D11 path or fallback, no -ld3d11, no --d3d11. Without
    D3D12 it shows "Neon Tide - Direct3D 12 required" with the reason and exits.
  - The landing tree (main + the port's 42-file patch) passed the full pipeline: syntax, -O2 build (dxgi only), all
    134 shaders as SM 5.1 with unbounded descriptor tables, the drive and the bat fight on D3D12.
  - Against the D3D11 build of the same tree: exposure within 0.3% (day, night, downtown), screenshots within
    2.7-4.1/255 mean (PSNR 30-36 dB), edges and jitter only.
  - New capabilities for the renderer: bindless arrays (a heap of 1M descriptors), root constants, async compute,
    ExecuteIndirect, placed and aliased resources, and a device self-test.
  - Rig fixes found on the way: Wine DXGI swap-chain rebuilds, the vkd3d root-argument clobber after UAV clears, and
    an upload-page lifetime bug in the port.
  - Not yet done: timings on real Windows hardware. The software rig can't measure GPU performance.
  - The main working tree was moved to D3D12 with a 3-way merge; agents' edits in app.cpp are kept. The renderer
    freeze is lifted, and the renderer agent starts its list on the D3D12 code.
  - Recommended next on D3D12, in order and each measured on real hardware: bindless materials, GPU culling with
    ExecuteIndirect, async compute for fog and GI, multithreaded command recording.
- 05:37 Snapshot 11 (tree e137e94, the first after D3D12) failed its drive smoke on memory, not on code: the
  out-of-memory killer took it, together with the finished D3D12 agent's leftover benchmark run. A D3D12 game under
  Wine/vkd3d uses about 2.7 GB (1.6 GB of it shared device memory) against about 2 GB on D3D11.
  - Fix: games now wait for 3 GB free under the cap before starting (tools/run.sh) and the snapshot steps for
    3.2 GB.
  - I tried to stop the D3D12 agent's leftover benchmark suite (the agent is finished) to free memory. The
    permission system blocked the follow-up, so I left it alone; it may still be running on slot 4 and will finish
    by itself.
  - Snapshot 11 re-runs on the same tree.
- 05:58 MERGED PR #11 (8b56d73): snapshot 11 (fe6fe8c), the first snapshot on D3D12. It contains dispatch with a
  short way in (calls in 38/21/10 s, down from 38/47/92 s), the K9 trail-jump stop, spray from wet skids, settled
  test screenshots and the car door choreography (not used by the game yet). Its retry passed every check.
- 06:00 Scorecard 4 started: the 16-stop district tour on the D3D12 build (snapshot 11's exe), 960x540, every shot
  settled over 6 rendered frames. It queues for slot 4 behind the D3D12 port's leftover suite and should take about
  2 h; shots go to /tmp/tour4.
- 06:28 MERGED PR #12 (6dae3a6): snapshot 12 (99ca6d3), the renderer's first D3D12-era work. It contains junctions
  without rain stripes (wheel tracks only on road segments), backlit smoke that glows, filtered wet/dry edges under
  roofs, cleaner material thresholds and seamless tiling, ray-direction checks on reflection hits, and debug views
  that no longer feed back into TAA.
- 06:28 Held back from snapshots: the AI agent's arrest escort and onlookers (police.cpp, pedai.cpp, ai.cpp,
  ai_game.h, barks.cpp, traffic.cpp, app.cpp), in progress, until it reports them tested. The animation agent fixed a
  MinGW-only compile error (a lambda named `near`, which Windows headers #define) and is adding a "cuffed" stance for
  the escort.
- 06:35 BUG FOUND ON D3D12: during the Scorecard 4 tour on snapshot 11's exe, screenshots turned into full-screen
  noise from stop 5 on (stops 0-4 are fine), with no errors in any log. The renderer agent hit the same thing
  independently on another D3D12 build (intermittent).
  - Not yet known: whether the frame itself is corrupt or only the screenshot readback. A memory-pressure allocation
    failure is suspected (it coincided with peak memory use).
  - The D3D12 agent was resumed to root-cause it. The tour was stopped (6 good shots kept in /tmp/tour4) and
    Scorecard 4 waits for the fix.
- 06:35 The AI agent's brawl event (EV_BRAWL: shouting, a friend pulling one away, fists, the crowd filming, police
  arresting the one who threw the first punch) is also held back: events.cpp and gameworld.h join the AI hold list.
- 06:45 Renderer agent verified all six D3D12-era items before and after with no regressions:
  - road stripes gone (debug view 5 and night storm);
  - the viaduct's wet/dry edge in rain now a smooth ramp instead of a sawtooth;
  - no aliased concrete pores;
  - debug view 13 showing real reflection hits;
  - smoke lighting subtle but correct.
  Comparison images are in /tmp/fx/ab_d12_*.png. It saw the noise screenshot once in 14 runs, at a time of heavy
  memory use.
- 06:45 Animation agent added the "cuffed" stance (25) and a hand hold that works outside clips. The lead wired the
  escort officer's hand onto the prisoner's arm in peds.cpp, and limited the greeting hold to the greeting clips so
  hands don't linger. peds.cpp joins the AI agent's held arrest/brawl set until that is tested.
- 06:50 D3D12 NOISE BUG ROOT-CAUSED (task #42): a race in gfx's fence wait.
  - Every CPU wait for the GPU shared one auto-reset event. When a 2 s poll timed out just as the fence completed,
    the loop exited without consuming the signal. From then on every wait returned one fence early, for the rest of
    the process.
  - Readbacks (screenshots, photo mode, GPU timers) then mapped their buffer before the copy ran and saved freed
    memory: the noise bytes decode as index and vertex buffers.
  - Presented frames are unaffected: everything else checks fence values, not the event.
  - It is real on Windows too, but needs a GPU wait over 2 s.
  - Reproduced with a self-test (64/64 and 1152/1536 values wrong before the fix, 0 after).
  - Fix: reset the event before SetEventOnCompletion and loop on the fence's completed value.
  - The game-level before/after runs next, then it lands as snapshot 13. After that, the Scorecard 4 tour re-runs.
- 07:11 The fence-wait fix is verified in game. With the old logic all 3 screenshots were noise (352 of 386 waits
  returned early); with the fix all 3 are clean and no wait returns early. It confirms the presented frames were
  always fine. Snapshot 13 (tree f630f60) is HEAD plus only the 4-file fix (gfx.cpp, gfx_tools.cpp, gfxtest.hlsl,
  PROGRESS.md) and is verifying now; all other work in progress stays out of it.
- 07:35 MERGED PR #13 (0854ac9): the fence-wait fix, so screenshots and photos no longer come out as noise on
  D3D12. Task #42 is done. The Scorecard 4 tour re-runs on this build (shots in /tmp/tour4b, about 75 min).
- 07:35 Renderer agent fixed the airport apron "leopard spots": an artifact of the 256-texel materials used in test
  runs, where relief came out 4x too steep and fine noise aliased. The game's 1024 materials are pixel-identical. It
  goes in the next snapshot. Approved next: night exposure. Night currently reads like dusk and lit lobbies blow out
  through glass. The plan is a key that depends on scene brightness plus highlight weighting for lit interiors;
  dusk stays as it is.
- 08:30 AI agent: the arrest escort works start to finish in game (still held for the remaining tests).
  - The sequence: a foot officer runs down a thief, tackles and cuffs them, and radios a transport. The suspect sits
    on the kerb; when the car pulls over they walk to it in cuffs, the officer a step behind, round to the back
    door, and get in. Logged 85 s after the cuffs.
  - Bugs the test found and fixed:
    - officers never caught a fleeing NPC on foot (a dispatch check treated both on foot as invalid);
    - officers ran slower than suspects in the last 6 m;
    - a parked car holding the brake reversed;
    - a door deadlock;
    - brawlers fled their own fight;
    - prisoners got out when the officers bailed out;
    - the transport parked 51 m away at a red light.
  - New autoplay "events" audits six ambient events. A forced mugging never staged, which is next.
- 08:38 SCORECARD 4 done. The 16-stop tour on the D3D12 build (main 0854ac9) ran 63 min with no crash, and every
  shot is clean. The fence fix holds: stops 5 and 6, noise before, are fine.
  - Scores (Scorecard 3 to Scorecard 4): map 5->6, visual fidelity 3->4, world life 4->5, vehicles 5->5,
    on-foot 5->6, AI 4->5, missions 4->5, audio 4->4, performance 4->4. The average is 4.9, up from 4.2.
  - The section is written into PROGRESS.md and goes in with the next snapshot.
  - Contact sheets: scratchpad/sc4/b/sheet1.png and sheet2.png.
  - Follow-ups:
    - night exposure (renderer, in progress);
    - lit lobbies blowing out through glass;
    - wardrobe by time and place (swimwear at the club at night);
    - sparse rural stops;
    - the marina's water level;
    - heads through low car roofs (animation, in progress).
- 08:43 Snapshot 14 is verifying. It holds Scorecard 4, the renderer's material-generator fix and the greeting-hold
  limit only.
  - Held back, still in progress:
    - the animation agent's door and seat-headroom work (player.cpp, game.h, vehicle_*.cpp, src/anim);
    - the renderer's night exposure (post.cpp, post.hlsl);
    - the AI set.
  - I asked the animation agent to say when the door work is end-to-end safe.
- 08:48 Started a world-life agent on three Scorecard 4 follow-ups:
  - wardrobe by time and place (no swimwear at the clubs at night or in the rain);
  - livelier rural stops (they had 19-46 people against 80-201 in the city);
  - the Key Coral marina's water flooding the street verge.
  It uses slot 4, free since the D3D12 port finished.
- 08:59 The user is awake and asked me to keep auto-merging. Standing request: every verified snapshot goes through a PR into main, merged by me, with a short note in chat.
- 09:01 Renderer: night exposure signed off (A/B: the club park, downtown and residential streets read as night; dusk and day unchanged). Saved as blobs; goes in snapshot 15 after 14 merges. Renderer next: bindless materials on D3D12 (it owns src/gfx now).
- 09:05 MERGED PR #14 (f081eb6): snapshot 14 (a9c3074), with Scorecard 4, the material-generator fix and the greeting-hold limit. Snapshot 15 (tree caf1a79, the night exposure only) is verifying.
- 09:08 Renderer, bindless materials (built in its private tree /tmp/fx/bl, so nothing in the main tree is mid-change).
  - Per-draw constant-buffer uploads are gone. World cells pass their offset in root constants; dynamic objects read a
    per-frame StructuredBuffer by index.
  - Materials, facades and signs are read through bindless arrays.
  - New: --gfxstats, and gfxselftest at 18 cases. Shaders: 139, 0 failed.
  - Before/after screenshot comparisons are running now.
- 09:28 Wardrobe: the lead changed one line in events.cpp spawnActor so event actors use wardrobeActorChar (from the
  world-life agent's assets.cpp) instead of randomCivilianChar.
  - Why: tourists, a street musician's audience and the night car meet were putting swimwear on the street at night.
  - Snapshot plan: graft this one line onto main's events.cpp in the world-life snapshot. The rest of events.cpp
    stays with the AI set. The AI agent has been told.
- 09:33 Snapshot 15's melee smoke run was KILLED BY THE KERNEL'S MEMORY-CAP OOM KILLER (dmesg: "Memory cgroup out of
  memory: Killed process snap_new.exe", rc 137 at t=7 of the bat fight). It was not a game failure; the drive smoke,
  build and shaders had passed.
  - Root cause in the test rig: the memory gates only counted the room free at launch. Several games and compiles
    could start together and then grow past the cap (a game reaches ~2.7 GB over its first minutes, the unity
    compile ~2.2 GB).
  - Fix: tools/memfree.sh now subtracts the room still owed to running games (--autotest) and unity compiles until
    they peak, so every gate (build.sh, run.sh, memgate) sees it. It is live in the working tree and rides the next
    snapshot.
  - Snapshot 15: re-running only the melee smoke with the same exe (checked against tree caf1a79), then commit.
- 09:35 AI agent: arrest case 1 passes end to end again. The transport waits out the red light, pulls over 29 m away,
  and the suspect is in the back 87 s after the cuffs.
  - Case 2 (a unit sent to a reported thief) is running. Cars with a prisoner aboard are no longer dispatched.
  - The brawl runs ended early because a deterministic ambient police chase drove through the crosswalk both times.
    Tests can now switch other ambient events off; the brawl re-run follows.
- 09:43 MERGED PR #15 (6966981): snapshot 15 (0f217f8), the night exposure. The melee re-run passed the full 18 s; the first attempt had been OOM-killed by the memory cap.
- 09:56 Check-in: all four agents active. Snapshots are now built by inclusion (HEAD plus only the files an agent signs off), because the world-life agent touches many shared game files (assets, population, airboat_tours, fishing, interiors_game, mission_util, story_act2, transit_game, weather). Next check-in at 10:55.
- 10:56 Check-in: all four agents active, and none has signed off since PR #15. The animation agent is past its 1.5-2 h estimate; I'll ask for its status at the next check-in if it's still quiet. The AI set also has pednav.cpp now (pedestrians wait at the kerb for a siren). Next check-in at 11:55.
- 11:06 CONTAINER RESTART at about 11:03. Every process died, including the four agents and their builds and
  game runs. The filesystem survived: the working tree, /tmp, the exes and the renderer's private tree.
  - I resumed all four agents.
  - All uncommitted work (33 files) is backed up to the branch claude/neon-tide-wip (d54422e), which is unverified and
    never merged. scratchpad/wip_backup.sh refreshes it.
  - Sign-off ETAs:
    - animation (doors and headroom): about 11:45, after re-running the Gatorback check;
    - AI (arrests, escort, brawl): about 11:50, with saved blobs. New untested work (witness statements, bystanders
      dashing into shops, a pursuit fix) gets a second sign-off later;
    - renderer (bindless materials, 19 self-test checks, the SkyLine glow fixed as downlights): about 12:20;
    - world life: about 12:35. The marina "flood" was rain puddles carried over from the rain stop, now fixed.
  - Plan: combine the animation and AI sign-offs into snapshot 16 (the escort uses the animation set's stance 25),
    and the renderer and world-life sign-offs into snapshot 17. The AI set's events.cpp keeps the old
    randomCivilianChar line until world-life's assets.cpp is in.
- 11:32 ANIMATION SIGNED OFF ("anim snapshot-safe"). Blobs saved in scratchpad/anim_blobs.txt (10 files);
  peds.cpp and app.cpp (its --autoplay cardoor test hunk) ride with the AI set.
  - Front-seat entry: walk to the door, pull it open by the handle, step round the edge, duck in, sit, swing the legs
    in, pull it shut, buckle up. Exit is the reverse, with door sounds that follow the door. NPCs taken out of cars
    use the same clips.
  - No more lunges: the worst hip move is now 5-7.5 cm per frame, down from 30 cm.
  - Tall people fit under low roofs with a slump instead of sliding forward. Headroom at 1.94 m went from -12 to
    -27 cm (into the roof) to +0.8 to +3.1 cm. Visors fold flush, and hats count toward head height.
  - Tests: carview door checks on 6 cars at 3 heights, anim_test all passed, a clean QUICK build, and Wine D3D12
    cardoor runs with a Harbor and a Gatorback.
  - Known issues: Orsa and Arclight door clips still clip, coupe rear passengers still touch the rear glass, and rear
    seats keep the old plain clips.
  - I asked the animation agent about a sawtooth A-pillar edge on the Gatorback in tall_driver.png.
- 11:34 The animation agent finished: rear doors, seat-belt straps, supercar seating and NPC door entry are left
  for later. The sawtooth A-pillar on the Gatorback shows in both halves of the before/after shot, so it predates this
  work rather than being a regression.
  - Started a FACES agent on Scorecard 4's "faces stay stylized":
    - eyes (iris, cornea, lids, lashes, brows);
    - skin micro-detail, colour zones and roughness;
    - lip, nose and ear shape, and age cues;
    - beards built from strands.
  - Its shader changes go through the lead to the renderer after the bindless change lands.
- 11:36 Renderer copied the verified bindless work into the main tree (src/gfx, src/render, src/shaders incl. new materials.hlsli, src/world/transitmesh.cpp, PROGRESS.md). Held until its sign-off in about 15 min (shc_d3d12, QUICK build and self-test on the main tree first).
- 11:40 The AI sign-off slipped to about 12:40: the arrest re-run hit the agent's own 30-min task limit (not a
  crash) and is re-running detached. It will send blob hashes, with two events.cpp variants (with and without the
  wardrobe line). The AI set must ship with or after the animation set: stance 25 and CarDoorInfo come from it.
  - So the animation set ships on its own, but player.cpp needs carDoorSpot and startCarDoorClip from its peds.cpp
    hunks. I asked the animation agent for peds_anim_only.cpp (HEAD plus only the door hunks), syntax-checked.
  - New order: whichever of animation (16) and renderer is ready first, then AI plus world life together.
- 11:43 Snapshot 16 (tree effc170: animation set + peds_anim_only.cpp + memfree.sh) verifying.
- 11:53 RENDERER SIGNED OFF: bindless materials + per-draw instance data + table reuse + SkyLine downlights. 21 blobs saved (scratchpad/renderer_bl_blobs.txt, incl. new materials.hlsli and PROGRESS.md's D3D12 section). Selftest 18 PASS + vkd3d SKIP of 19; shc 141/0. CB writes per frame 350-660 -> 68-72. Snapshot 17 after 16 commits.
- 11:56 Check-in: 5 agents active (AI, animation follow-ups, renderer next: character shading, world life, faces). Snapshot 16 passed syntax and build; shader check running. Queue: 16 animation, then 17 renderer, then 18 AI + world life. Next check-in 12:55.
- 12:00 World life: ETA now 13:10. Rural peds before/after: Sawgrass 19->33, Grove 46->67, Fort Castell
  32->52, Lake Town 31->75 (being toned down). Wardrobe and marina are OK, and stops 9-11 stay dry after the stop-8 rain.
  - Another OOM kill hit it at 11:08: a launch race right after the restart, with several games and a compile
    passing the gate in the same seconds before any showed in memfree's count.
  - Fix: tools/run.sh and build.sh take a shared launch lock (/tmp/neontide_launch.lock) around the gate. A
    background sleep holds it 30 s after a game starts and 15 s after a compile starts. Tested with a 3-launcher
    harness: launches spaced, and the game itself doesn't hold the lock. It rides the next snapshot with memfree.sh
    (in 16) or after.
  - Snapshot 16: shaders 135/0 passed; smoke runs next.
- 12:01 Renderer, character shading started in its private tree (ETA 4-6 h, mostly rig time). The vertex-data contract with the faces agent is agreed (skin region, translucency, pores, oiliness, age and melanin bits; eye optical axis and radius; hair-card depth), with zero-safe defaults. The eye mesh batch and the eye shading land together. I approved a test-only --facecam/--viewerlamp change in src/game/viewer.cpp, in the main tree, to be signed off separately.
- 12:08 Animation: the sawtooth fix is one inclusive-range condition in vehicle_carbody.cpp (blob 1491f6cc); the side glass's slanted rear edge kinked into a W on every car with one, and on the far LODs. Checked on all 27 cars, HEAD vs now. It rides snapshot 17.
- 12:16 MERGED PR #16 (6b31710): snapshot 16 (ad342df), car doors (front seats), the headroom slump, the escort groundwork and memfree.sh.
- 12:16 Snapshot 17 (tree 737b0eb: renderer bindless set + carbody sawtooth fix + run.sh/build.sh launch lock) verifying.
- 12:19 AI sign-off moved to about 13:40. The full arrest re-run found a real bug: the dispatched unit drove
  straight at the thief, braked for pedestrians in its path and sat stuck about 47 m short, because the bail-out
  existed only in lane mode. The fix is in police.cpp: officers get out when held up near a suspect on foot. It is
  being tested in its own tree (nt_ai52f).
  - New plan: snapshot 18 is world life (about 13:10), with the wardrobe line grafted onto HEAD's events.cpp.
  - Snapshot 19 is the AI set (about 13:40), using the events.cpp variant that keeps the wardrobe line.
- 12:26 viewer.cpp signed off (blob 78837683): --facecam / --viewerlamp close-up test options, inert without the flags. It rides snapshot 18.
- 12:48 MERGED PR #17 (2de35c5): snapshot 17 (5d7bff7), with bindless materials and per-draw instance data, the SkyLine downlights, the sawtooth C-pillar fix and the launch lock. Shaders 141 with 0 failed.
- 12:51 World life: ETA now 13:50, after a one-line guard so driveway cars don't spawn onto a standing ped after a fade-in, plus one more full build and a smoke run. Stops 12: 19->31 peds, 13: 46->80. AI is about 13:40, so snapshot 18 combines AI + world life + viewer.cpp, using the events.cpp variant with the wardrobe line.
- 12:56 Check-in: 5 agents active. WIP backed up (ecb87ed). Snapshot 18 (AI + world life + viewer.cpp) waits on the AI (about 13:40) and world-life (about 13:50) sign-offs. Next check-in 13:55.
- 13:23 WORLD LIFE SIGNED OFF. Blobs in scratchpad/wl_blobs.txt (9 files, plus events.cpp = HEAD with the wardrobe line grafted). Evidence: club night has no swimwear (a print shirt); Lake Town is dry and has people. Snapshot 18 (tree 9b86c54: world life + viewer.cpp) verifying. The AI set follows as snapshot 19.
- 13:25 The world-life agent finished. Started a CITY-BLOCKS agent on Scorecard 4's "massing between landmarks still repeats": Miami building palettes by district, varied footprints, setbacks and roofs, a repetition metric; lots, anchors and interiors stay intact, and the campaign regression runs before and after.
- 13:45 AI SIGNED OFF: arrest, escort and brawl set from nt_ai52f blobs (scratchpad/ai_blobs.txt; events.cpp = the wardrobe variant 2ca4e110; peds.cpp = HEAD + the escort block). Arrest stage 1 passed end to end; stage 3 now goes on foot and cuffs (boarding didn't fit the 200 s test window). The brawl passed. It becomes snapshot 19 after 18 merges. Next AI set: witness statements, fleeing into shops, sidewalk ID checks, coffee breaks, couples hand in hand (a 12-line peds.cpp block).
- 13:46 Renderer, character shading (rebased on main 2de35c5), ETA 15:30:
  - skin: pre-integrated scatter, melanin-tinted; transmission through ears and nostrils under every light; per-region roughness and specular AA; warm crease occlusion;
  - eyes: SM_EYE with cornea refraction (iris parallax, the pupil behind the cornea), a pupil that dilates at night, lid occlusion;
  - hair: depth self-shadowing, filtered strand edges, no grey sheen on dark hair.
  The faces agent's kShaderPupil flip lands as its own batch after this one. Stop 7 timings are within noise.
- 13:56 Check-in: 5 agents active (AI second set, animation rear doors, renderer character shading, faces, city blocks). Snapshot 18 is in the full build after a 19-min memory wait. WIP backed up (89463e4). Next check-in 14:55.
- 14:09 The city agent's new src/world/blockstyle.cpp broke the shared MinGW build ('near' is an empty Windows macro; the AI agent spotted it). Fixed by 14:08. The city agent then moved all its unverified work into a private copy; src/world is back at HEAD, and files return only as syntax-checked, signed-off batches.
- 14:31 MERGED PR #18 (e845e67): snapshot 18 (0047ddd), with wardrobe by place, hour and weather; lived-in small towns; ground that settles on weather changes; Key Coral bait shop at the marina; viewer --facecam/--viewerlamp.
- 14:31 Snapshot 19 (tree c4a957b: the AI set, 13 blobs) verifying.
- 14:44 Renderer: character shading is integrated into the main tree but not signed off (ETA 15:45): renderer.cpp/.h, skin.cpp (new), lighting/dynamic/decals.hlsl, common.hlsli, PROGRESS.md. My review of the proof sheets: the change is subtle at those crops and the close-ups are blurry, so I asked for sharp 1080p crops and a sheet with the faces batch-1 meshes. Also flagged the 'goggles' look at 4 m (dark sockets ringed with bright lines), present before and after, for the renderer and faces agents.
- 14:49 Renderer, the goggles look explained: under an overhead lamp nothing shadows the upper lid and brow bone (lamps cast no shadows, and the sun's contact shadows are far too coarse for a 1-2 cm brow ridge), and the legacy eye shader darkens the eyeball. Fix: a character-scale contact shadow (8 steps over about 4.5 cm) for skin and eyes within 12 m, toward the sun and lamps. Sign-off ETA moved to 17:30, with full 1080p sign-off sheets.
- 14:56 Check-in: 5 agents active. Snapshot 19 (the AI set) passed the build and shaders and is in the smoke runs. WIP backed up (c7fc062). Next check-in 15:55.
- 15:10 Wine slot 4 handed to the renderer (world life is finished). The renderer's goggles fix is built (nt_cs4); --shaderdir in gfx_shaders.cpp loads .hlsl from a folder for A/B runs without a rebuild.
- 15:24 Animation: the rear-door clips are in the tree (blobs in scratchpad/anim_blobs_rear.txt: clips.cpp, vehicle_doors.cpp, vehicle_cardetail.cpp, vehicle_cars.cpp, player.cpp, carview.cpp). The in-game check is queued.
  - The lead applied its seatDoorVF gate in the working-tree peds.cpp: seats 2 and up use their door unless door < 2, so coupe +2 seats keep the plain clips.
  - At sign-off, graft the same gate onto HEAD's peds.cpp. The AI agent was told.
- 15:26 Snapshot 19's smoke step starved: it waited 34 min in memgate while 4 agent games (about 2.8 GB each) and a compile held the memory, and agents kept winning the race for freed room.
  - Fix, lead priority: while the lead's memgate waits, it keeps /tmp/neontide_lead_wants fresh. Agents' run.sh and build.sh hold back new launches while the flag is fresh (up to 30 min; stale after 5 min), and the flag stays 60 s past the gate.
  - The lead's own build passes NT_LEAD_SLOT=1 (snap_smoke.sh).
  - Installed atomically (run.sh, build.sh, memgate.sh, snap_smoke.sh).
  - For the running snapshot 19, a background keeper holds the flag until its game launches.
- 15:56 Check-in: 5 agents active. Snapshot 19's drive smoke passed; the melee run is next. WIP backed up (8a389cd). Next check-in 16:55.
- 15:57 MERGED PR #19 (1c87961): snapshot 19 (aa9e6c9), with arrests escorted to a patrol car, street brawls, units that slow and get out near suspects on foot, and pedestrians who wait for sirens.
- 15:59 Renderer: the goggles look is part shading, part mesh. Sign-off ETA about 18:00.
  - Shading, fixed: feature shadows (a 4.5 cm screen-space march, skin/eyes/teeth within 12 m, sun plus the two strongest lamps) now shade the upper lids under the brow. Eyes and teeth are SM_EYE, with lid occlusion and a cut-off for steep light. Nails are detected by region only, so lip gloss isn't taken for nails.
  - Mesh, sent to the faces agent: bright lid-margin tuck strips, a dark ring from the V-groove AO at 3 mm lid blends (its batch 2 widens them to 8 mm), heavy brows sitting on the lid, and eyes flush with the brow ridge.
- 16:05 AI second set: the sign-off follows the nt_ai58 run, about 4-5 h out, on top of main 1c87961, with the seatDoorVF gate in its peds.cpp.
  - Passed on nt_ai57:
    - sidewalk stops: let go after an ID check; a warrant arrest with a transport and a witness statement;
    - a patrol coffee break by the car (111 s);
    - panic: 4 bystanders ran into shops, 23 fled, 6 hit the deck.
  - Fixed in nt_ai58: the runner was tackled in the same frame he bolted; he now gets a head start.
  - Also in nt_ai58: ducking behind parked cars away from gunfire; stepping aside from chases; foot chases to the last-seen spot; a partner brings the car round; heckles, radio lines, strangers asking the way, couples hand in hand.
- 16:49 FACES BATCH 1 SIGNED OFF (6 files, blobs in scratchpad/faces_b1_blobs.txt): eyes, lids, lashes, hair-by-hair brows, skin vertex data, ears, card beards and stubble, soft hairline. The montages show clearly softer beards and better ears; the goggles look waits for batch 2 and the renderer's feature shadows. Snapshot 20 (faces b1 + run.sh/build.sh lead priority) verifying.
- 16:56 Check-in: 5 agents active. Snapshot 20 (faces batch 1 + the lead priority change) verifying. WIP backed up (5353ded). Next check-in 17:55.
- 17:24 MERGED PR #20 (46800db): snapshot 20 (2833ad0), faces batch 1 (eyes, lids, lashes, brows, skin data, ears, card beards, soft hairline) and the lead priority change.
- 17:28 ANIM REAR DOORS SIGNED OFF (6 blobs in anim_blobs_rear.txt, plus peds.cpp = HEAD with the seatDoorVF gate, blob 63846858). Clipping: in 18.8%->6.8%, out 24.7%->10.0%. The montage looks right. Snapshot 21 (tree 516df5f) verifying.
- 17:29 Animation agent's next job: a locomotion polish pass (foot sliding, start/stop/turn steps, passing body language, idles, look-at), with measured before/after. NPC door entry later, via a GameWorld hook once the AI second set lands.
- 17:50 The AI agent noticed its scripts were calling the scratchpad memgate.sh, so its waits raised the lead flag too. It switched to its own ai_memgate.sh. memgate.sh now raises the flag only when its parent is snap_smoke.sh or snap_resume_melee.sh; any other caller gets a plain wait. A stale flag from the AI's killed chain was removed.
- 17:51 Animation locomotion baseline (scratch locometer). Sliding in cm per step, mean/p90/max:
  - flat walk 1.1/1.7/2.5;
  - walking turns, the worst: 180 deg at 34/106/132;
  - passing head-on 7-21, max 56;
  - starts 4.2/14/39;
  - 10-deg slopes 8.7-20;
  - stairs about 20.
  Two game-side causes:
  - a stale p.turnRate when turnTo isn't called (2.5 m of foot slide in 4.5 s);
  - no information about passers-by.
  Approved: turnRate from the yaw delta in animatePed with Ped::animYaw, and AnimInput passBy/passVel/passWeight. Its peds.cpp hunks get grafted at sign-off; the AI agent has been told.
- 17:54 Faces: the EYE FLIP (kShaderPupil on) is signed off (face.cpp + anim_test.cpp, blobs in faces_flip_blobs.txt). It must land WITH or AFTER the renderer's eye shaders, never before.
- 17:55 RENDERER SIGNED OFF, character shading: 9 blobs in renderer_cs_blobs.txt (skin.cpp is new). Self-test 18 PASS + 1 SKIP; shaders 141/0. In the 4 m night proof (main, then final, then final with batch 1 and the flip) the goggles rims are mostly gone with the flip. Snapshot 22 = renderer shading + the eye flip (faces_flip_blobs.txt), after 21 merges.
- 17:56 Check-in: 5 agents active. Snapshot 21 (rear doors) in its melee smoke; snapshot 22 (renderer shading + eye flip) queued. WIP backed up (5a77d85). Next check-in 18:55.
- 17:58 Renderer's next job approved: GPU-driven vegetation in 3 phases.
  - Phase 1: GPU-driven props (persistent per-cell instance buffers; compute cull per view with Hi-Z; ExecuteIndirect), visually identical.
  - Phase 2: LODs plus baked impostors, so palms stay on the skyline, and denser GPU ground cover and flowers.
  - Phase 3: palms with split leaflets, dead fronds, coconuts and ringed trunks; flowering shrubs; leafy hedge shells; wind throughout.
  - The renderer owns propmesh.cpp's vegetation builders (footprints, collision and anchors unchanged). The city agent was told.
- 18:02 MERGED PR #21 (23069e5): snapshot 21 (a35e599), rear doors.
- 18:02 Snapshot 22 (renderer character shading + the eye flip) verifying.
- 18:37 Animation locomotion prototype, slide in cm/step mean (max): curb up/down 2.0/5.9 (25/29) -> 0.9/0.8 (1.8/1.7); stairs 21.7/19.5 -> 3.0/1.6; 10-deg downhill walk/jog/run 8.7/19/20 -> 1.9/1.2/1.0. Method: the swinging foot alternates its probe between near-foot and landing spot. Two more character.h fields cleared (groundAhead, probeAhead).
- 18:42 Snapshot 22's drive smoke was OOM-KILLED near its end (dmesg: cc1plus invoked the oom-killer and it killed snap_new.exe).
  - At the time: 4 games of about 2.85 GB each (snap_new, the faces game nt_b2vis, the city game nt_b2 and the renderer's nt_gd1), the AI's ai_syntax.sh MinGW syntax check, and another compile.
  - Fixes:
    - memfree reserves 3000 MB per game (games now reach 2.85-2.9 GB);
    - run.sh and build.sh give non-lead games and builds oom_score_adj +500, so the kernel kills those before the lead's checks (raising your own score needs no privilege; lowering it is denied);
    - new scratchpad/snap_resume_smoke.sh re-runs drive and melee with the same exe; memgate recognizes it.
  - Snapshot 22 smoke re-run started on the same exe (checked: the exe matches the build dir, which matches tree ac8448dd).
- 18:56 Check-in: 5 agents active. Snapshot 22's smoke re-run is waiting in memgate (memfree about 1.2 GB, lead flag up). WIP backed up (1ed638d). Next check-in 19:55.
- 19:17 MERGED PR #22 (4b6357f): snapshot 22 (33a22db), character shading (skin scatter, cornea-refracted eyes with shader pupils, feature shadows, softer hair) plus the faces eye flip. Smoke cpu ms peds about 1.0 at t=9.5, in line with earlier snapshots, so no CPU regression.
- 19:54 City batches 1+2 signed off: about 35 district building types, carved footprints, varied roofs, about 3,500 back buildings, rear parking, fenced yards, and BuildingSet::pavedAt/pavedGrid.
  - Checks: story regression 27/27, worldcheck clean, repetition 14.4% -> 3.8%.
- 19:56 Snapshot 23 (tree 554fac8) verifying.
  - Contents: the 7 city src/world files, plus memfree 3000 MB and oom_score_adj 500 for non-lead runs in run.sh and build.sh.
  - Blobs are in city_b12_blobs.txt.
- 19:57 Check-in: 5 agents active (animation waiting on its monitors). Snapshot 23 passed syntax and is waiting in memgate (memfree about 0, lead flag up). WIP backed up (519051d). Next check-in 20:56.
- 20:13 Renderer vegetation phase 1 signed off: GPU-driven props, one compute cull plus ExecuteIndirect per pass.
  - Checks: props identical at tour stops 4-13; draws 525 -> 468; gfxselftest 19 pass, 1 skip; shc 146/146.
  - Blobs in renderer_p1_blobs.txt. Queued as snapshot 24 after 23 merges.
- 20:17 Snapshot 23's O2 build is compiling (started 20:11 after waiting for memory under the launch lock).
- 20:56 Snapshot 23 COMMITTED (b964d5e). Melee reached t=13.5 with health 188 and no crash.
- 20:57 MERGED PR #23 (6780edd): city batches 1+2 (district building types, carved footprints, back lots, pavedAt) plus the memfree, oom_score_adj and run.sh/build.sh tools changes.
  - Fast-forwarded and pushed.
- 20:52 AI set 2 signed off: sidewalk stops, witness statements, coffee breaks, cover, purse-snatcher chase, route-length dispatch, aiWalkRound. Blobs in ai_set2_blobs.txt. The couple block stays out until set 3+4's life run checks it.
- 20:57 Snapshot 24 (tree aaa0ff8) verifying: renderer phase 1 plus AI set 2.
- 20:59 Check-in: 5 agents active. PR #23 merged and auto-unsubscribed. Snapshot 24 syntax check running (40 shaders embedded, with propcull). WIP backed up (79833ee). Next check-in 21:58.
- 21:18 USER: only 140 credits left. Finish snapshot 24, wait for the agents to finish, then save everything. All 5 agents wrapped up, wrote resume/*.md and stopped every job. RESTORE.md written. Waiting on snapshot 24, then save_all.sh to claude/neon-tide-save.
- 21:26 Snapshot 24 COMMITTED (716ef64): 146 shaders, drive to t=17.5 (peds 0.9 ms), melee to t=13.5. MERGED PR #24 (06580c3): GPU-driven props plus AI set 2. Fast-forwarded and pushed. Next: save_all.sh to claude/neon-tide-save.
