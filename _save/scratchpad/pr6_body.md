## Summary
One build-verified snapshot (`757aaae`), merged overnight by the lead at the user's request.

- **Characters:**
  - Hair: cornrows, box braids and locs (long locs, short locs and twists), built from ropes. Some are in braiding
    hair of their own colour: burgundy, honey blonde, copper or jet black.
  - Locs and box braids don't go under a hat; cornrows do, with the rows running back level above the ears.
  - Tank-top and dress straps lie taut over the collarbone and shoulder blade instead of folding into the hollows.
- **Animation:**
  - An aiming side-step jog and a back-pedalling jog.
- **Injuries:**
  - A hit flinches the body by bone and direction, layered over whatever the legs are doing, instead of a full-body
    hit clip that stopped the person.
  - Heavy hits (rifle, bat) knock the body off balance, and it catches itself with quick steps.
  - Pedestrians shot in the leg limp on that leg. Badly hurt ones walk hunched with laboured breathing and hold the
    wound with a free hand.
  - The player keeps full control: flinch and stagger only.
- **Police and traffic:**
  - A unit held up in traffic out of the player's sight is moved on past the jam.
  - A unit stuck close to a suspect on foot gets out and goes on foot.
  - Drivers at a crossing hold at the line for a siren coming through.
- **Interiors:** the interior a story scene is staged in stays loaded for the scene, so a cutscene camera that starts
  far away no longer pushes the people in it out through the building's shell.
- **Hospitals:** the map blips, paramedic drop-offs and the respawn after being wasted use the hospitals as the world
  built them, so you no longer respawn in the sea off Fort Castell.
- **Map:** landmarks (Ocean Promenade, Porto Sol University, Santa Marea Cemetery, the churches, the penitentiary, the
  speedway) are labelled in a tier under the districts, shown when zoomed in.
- **Missions and tests:**
  - The bait-shop market test runs at night with an approach frame.
  - Paper Trail's autotest looks at the safe marker before walking in.
  - traffic_sim gets place anchor dumps.
- **Test rig:** builds and Wine runs wait for room under the container's memory cap (`tools/memfree.sh`), and at most
  three shared games run at once, so compiles are no longer killed for memory.

## Test plan
- [x] The whole unity build passes a syntax check.
- [x] The full `-O2` build compiles and links.
- [x] Every shader entry point compiles.
- [x] A first-person drive smoke run under Wine completes.
- [x] Agent-level tests on their own builds:
  - the animation suite, including the new Impacts test;
  - the clothing clip tests;
  - the traffic_sim police response harness.

🤖 Generated with [Claude Code](https://claude.com/claude-code)

https://claude.ai/code/session_01Fsx2GTrhwusdowbWY9LHRP
