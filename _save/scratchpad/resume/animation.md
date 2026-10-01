# Animation agent: resume notes (locomotion polish pass)

SP = /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad
Tree = /home/user/GTA-6-Claude-v0.5 (HEAD 4b6357f, merge of PR #22)

## 1. State

- Merged: my earlier work (rear doors, seatDoorVF gate) is in snapshots 21 and 22. The locomotion pass has NOT been
  merged yet. It sits uncommitted in the shared tree, in 4 files:
  - src/anim/animator.cpp: blob ec04f5e75e02922a62ec7708206f049a1fd20257. This is my file; take it whole.
  - src/anim/character.h: blob 08062c975df26934f0d70e21541c6d13e02cd590. Additive fields only (listed below).
    The file is shared with the faces agent; at HEAD it holds only my additions.
  - src/game/peds.cpp: blob 6bb51bd2c242dbdbb05f685c8b705876c847fb48. Its diff against HEAD is exactly my two
    animatePed hunks. Graft them as text (section 2).
  - src/game/game.h: blob 4fad4bbd288461d69453a02eebfc97af33341fa6. Its diff against HEAD is the one animYaw line.
    Graft it as text.
  - The same diffs as patches: SP/loco/anim_files.patch, SP/loco/peds_hunks.patch, SP/loco/gameh_animYaw.patch.
- Verified:
  - Locomotion harness: before/after numbers in section 4.
  - anim_test on the real tree: ALL PASSED (SP/loco/at_final.log).
  - Whole-unity MinGW syntax check, with HEAD + my 4 files + the test-only locotour patch, -DLOCO_AFTER: clean.
  - Full O2 build of that tree: no warnings. It ran 39 s of game time in Wine without trouble.
  - My verdict on the 4 files: anim snapshot-safe.
- Not finished:
  - The Wine run at tour stop 2: the BEFORE run is complete. The AFTER run was stopped at the wrap-up after burst 2
    of 8, so there are no in-game "after" stats yet.
  - The sign-off message to the lead has not been sent.

### character.h fields (additive, cleared by the lead and the faces agent)

AnimInput, after `bool footProbes = false;`:

    vec3 passBy = vec3(0); vec2 passVel = vec2(0); float passWeight = 0;

Animator, after `float legSink = 0.f;`:

    float groundRaw[2]; float rootVz; bool plantedPrev[2]; bool footHold[2]; float pivotW[2];
    float groundAhead[2]; bool probeAhead[2]; float passW, passYaw, passShift;

The exact text with comments is in SP/loco/anim_files.patch.

## 2. Text to graft onto main (peds.cpp, game.h)

game.h, struct Ped, after `float turnRate = 0.f;` in "animation extras":

    float animYaw = 0.f;          // yaw at the last animation update (animatePed: the turn rate the animator gets)

peds.cpp, GameWorld::animatePed, hunk 1. It replaces the line `in.turnRate = p.turnRate;` (right after
`in.speed = spd;`):

    // the turn the animator gets is the yaw's own change since the last animation update, whatever turned the ped (the
    // AI's turnTo or faceTowards, a script setting the yaw, a stop in mid-turn); a radian or more at once is a warp
    {
        float dy = wrapAngle(p.yaw - p.animYaw);
        in.turnRate = dt > 0.f && fabsf(dy) < 1.f ? dy / dt : 0.f;
        p.animYaw = p.yaw;
    }

peds.cpp, GameWorld::animatePed, hunk 2. It goes right before the comment
`// foot IK: probe the ground where each foot is planted / about to land ...`:

    // someone on foot passing close (the animator turns the near shoulder back and side-steps): of the people near the
    // camera, the nearest one whose closest approach over the next 1.5 s comes within a metre
    in.passWeight = 0.f;
    if (upright && p.grounded && p.visibleDist < 30.f) {
        float best = 2.6f;
        int bj = -1;
        vec2 brv(0.f);
        for (int j = 0; j < (int)peds.size(); j++) {
            const Ped& o = peds[j];
            if (o.visibleDist > 33.f || !o.used || &o == &p || o.state != PS_ONFOOT || o.ragdoll || o.health <= 0.f) continue;
            vec3 D = rel(o.pos, p.pos);
            vec2 d2(D.x, D.y), rv(o.vel.x - p.vel.x, o.vel.y - p.vel.y);
            float dist = length(d2), rv2 = length2(rv);
            if (dist >= best || fabsf(D.z) > 1.2f) continue;
            float tca = rv2 > 1e-3f ? Clamp(-dot(d2, rv) / rv2, 0.f, 1.5f) : 0.f;
            if (length(d2 + rv * tca) > 1.f) continue;
            best = dist;
            bj = j;
            brv = rv;
        }
        if (bj >= 0) {
            const Ped& o = peds[bj];
            vec3 c = rel(o.pos, p.pos) + rotate(yawQuat(o.yaw), o.bones[Anim::B_CHEST].c[3].xyz());
            in.passBy = vec3(dot(vec2(c.x, c.y), rightV), dot(vec2(c.x, c.y), fwd), c.z);
            in.passVel = vec2(dot(brv, rightV), dot(brv, fwd));
            float k = Saturate((2.4f - best) / 1.3f);
            in.passWeight = k * k * (3.f - 2.f * k);
        }
    }

(`upright`, `fwd` and `rightV` are already defined earlier in animatePed. spawnPed calls animatePed(p, 0.f), which
syncs animYaw; the 1 rad guard covers warps.)

## 3. What the pass changed (animator.cpp)

- Planted feet stay put. Ground heights are kept with the world (the root's vertical motion is read off planted
  feet). Swinging feet probe where they will land, every other update. The toe tip counts as a contact. A foot let
  go at toe-off is held until it lifts.
- Walking turns: a twisted planted foot pivots on its ball, with the heel raised. A foot left out of comfortable
  reach takes a quick turn step if the other foot is down; otherwise the other foot's step is hurried.
- Starts: the gait starts with the unloaded foot in mid swing. Stops: blends keep the swing's clearance.
- Turn rate: it comes from the actual yaw change (peds.cpp hunk 1). Before, it was stale or missing for
  faceTowards and scripted turns.
- Passing someone close (peds.cpp hunk 2 feeds AnimInput::passBy): the near shoulder turns back (up to 0.5 rad,
  mostly taken back by the neck and head), the trunk leans away 0.2 x the turn with the arms kept hanging plumb, and
  the feet side-step up to 11 cm.
- Look-at and idle: measured only, unchanged (the numbers are fine).

## 4. Measurements (before = HEAD animator, after = the tree)

Harness: SP/loco/locometer.cpp + locosim.h. Results: SP/loco/base_final.txt and SP/loco/work_final.txt. Compare with
`python3 SP/loco/cmp.py base_final.txt work_final.txt`.

Sliding cm per step, mean / p90 / max:

| Case | Before | After |
|---|---|---|
| walk 1.4 | 1.14 / 1.69 / 2.55 | 0.94 / 1.69 / 2.08 |
| slow walk 0.9 | 1.26 / 3.57 / 4.73 | 0.74 / 1.79 / 3.25 |
| jog 3.0 | 0.74 / 1.83 / 3.62 | 0.51 / 1.66 / 2.59 |
| run 5.0 | 0.49 / 1.53 / 2.57 | 0.49 / 1.53 / 2.80 |
| 10% hill, walk down | 8.65 / 12.5 / 15.2 | 1.93 / 2.95 / 3.86 |
| 10% hill, jog down | 19.3 / 24.3 / 26.8 | 1.21 / 2.43 / 2.97 |
| 10% hill, run down | 20.1 / 25.5 / 27.0 | 1.02 / 1.47 / 1.49 |
| 15 cm curb, down | 5.86 / 21.1 / 28.9 | 0.78 / 1.34 / 1.67 |
| 15 cm curb, up (max) | 25.4 | 1.80 |
| stairs up | 21.7 / 62.1 / 75.2 | 2.97 / 8.66 / 9.59 |
| stairs down | 19.5 / 58.2 / 63.6 | 1.56 / 5.28 / 7.09 |
| start | 4.16 / 14.1 / 38.6 | 0.92 / 1.89 / 6.11 |
| stop, walk | 1.22 / 3.21 / 18.3 | 0.62 / 1.76 / 5.41 |
| stop, jog | 4.96 / 18.2 / 37.2 | 2.00 / 7.88 / 15.6 |
| walking turn 45 | 11.3 / 47.6 / 57.3 | 1.35 / 2.75 / 6.42 |
| walking turn 90 | 16.9 / 54.3 / 74.2 | 1.39 / 3.38 / 8.68 |
| walking turn 135 | 25.4 / 75.3 / 104 | 1.63 / 4.02 / 8.50 |
| walking turn 180 | 34.5 / 107 / 132 | 1.24 / 2.36 / 14.8 |
| turn on the spot 180 | 0.22 / 0.99 / 1.14 | 0.06 / 0.12 / 0.24 |
| head-on pass, 0.2 m offset | 7.49 / 23.0 / 56.3 | 1.36 / 2.44 / 4.47 |
| head-on pass, 0.5 m offset | 21.0 / 43.8 / 55.6 | 1.28 / 4.78 / 5.92 |

Other changes:

- Hills: on 10% climbs the base build had its planted feet floating (1798 / 584 / 257 float frames, up to 85 mm).
  After: walk 11 frames, jog and run 0.
- Starts: first lift 0.256 s mean, then 0.122 s. The unloaded foot steps first 56% of the time, then 100%.
- Passing: closest arm gap 7.2 / -2.0 cm (the arms went through each other), then 15.2 / 13.1 cm. Chest turn at
  the pass 17 / 16 deg, then 19 / 30 deg.

Remaining:

- Sharp walking turns (90 to 180 deg at 1.4 m/s): the trailing planted foot can show off the ground for about 4
  frames a turn (35 / 71 / 62 frames over 18 runs at 60 Hz, up to 10-16 cm). In the base build it slid instead.
- Toe pivots dip about 8 mm into the ground.
- A foot straddling a curb or stair edge: lowest sole point -150 / -194 mm. That needs more probes.
- The jog stop's worst step is still 15.6 cm.
- An experiment (drag the out-of-reach footprint, land the other foot early) measured worse everywhere and was
  reverted. Its results: SP/loco/work_reach.txt, patch SP/loco/patches/p9_reach.py.

Look-at (unchanged):

- Siren: the head reaches 90% in 0.42 s, the eyes lead by 0.09 s, overshoot 2.3 deg.
- Walk-by at 2 m: gaze error 3.6 deg mean, head lag 3.4 deg.
- Run-by (3 m/s at 1.2 m): gaze error 4.8 deg mean / 9.8 deg max, head lag 11 deg. The eyes cover the lag.
- Game feed: the player passing (35%, 70% if armed), sirens and horns (85%), passing cars (35%), conversations.
  Fights have no look feed; that is AI/peds.cpp work.

Crowd idle (unchanged), per person-minute: 6.7 weight shifts, 4.7 settle steps, 3.8 postures, 2.4 fidgets,
7.6 glances. Longest still stretch 8.4 s mean / 11.7 s max. 33% of shifts fall within 0.25 s of someone else's
(random would be about 57%), so there is no lockstep.

anim_test (SP/loco/at_final.log vs at_base.log):

- ALL PASSED.
- Stance skate p95 0.050 -> 0.035 m/s; walking back 0.003 -> 0.000; back-left 0.004 -> 0.000.
- Shins at least 0.131 -> 0.137 m apart.
- Limp planted feet 0.0004 -> 0.0014 m/s.
- The animator update is about 4 us per walking ped, the same as before within noise. The machine was loaded:
  base 3.8-7.6 us, after 4.0-4.4 us.

In game, tour stop 2 (downtown, 14:00), BEFORE build, SP/locotour/log_before.txt:

- Steps n 826: slide cm/step mean 14.9, p90 41.6, max 459. Scuffs 44.
- Walking straight: 4.97 / 10.1 / 213.
- Walking and turning: 30.5 / 71.9 / 459.
- Standing: 13.5 / 40.9 / 78.
- Jog/run: 18.6 / 51.5 / 80.
- Planted but off the ground: 314 foot-frames. Lowest sole -299 mm.
- 28 close passes: arm gap smallest 0.6 cm, mean 13.7 cm.
- Peds CPU 1.73 ms/frame.
- The AFTER run is still to do. The runs are deterministic: the after run's bursts 0-3 picked the same peds and
  camera spots at the same times as the before run, so the montages compare one to one.

Harness pictures: SP/loco/viz/turn90.png (top before, bottom after: red = slide traces), pass.png, pass_top.png.

## 5. Paths

- Harness: SP/loco/locometer.cpp, locosim.h, lmviz.cpp, mk.sh (`sh mk.sh <tree> <out>`, gated), mkviz.sh,
  final.sh (both locometers + anim_test), cmp.py, floats.py, patches/.
- Harness trees: SP/loco/base (HEAD animator + additive character.h) and SP/loco/work (= the tree's anim files).
- Harness binaries kept: SP/loco/lm_base, lm_work, at_final.
- Wine test (test-only, never for main):
  - Patch: SP/locotour/block.cpp (the updateLocoTour etc. code) and apply.py (adds `--autoplay locotour` to HEAD's
    app.cpp).
  - Trees: SP/locotour/before (HEAD + patch) and SP/locotour/after (HEAD + my 4 files + patch, built with
    -DLOCO_AFTER).
  - Exes: SP/locotour/before/bin/loco_before.exe and SP/locotour/after/bin/loco_after.exe (O2, about 12 MB each;
    delete them after the runs).
  - Scripts: SP/locotour/build.sh, run.sh, runall.sh, montage.sh, check.sh.
  - Logs: SP/locotour/log_before.txt, log_after_partial.txt, run_before.txt, run_after.txt, build_*.log.
  - Shots: SP/locotour/shots_before (96 PNG) and shots_after (36 PNG, bursts 0-2).
  - Montages: SP/locotour/montage/before_b00..b07_*.png and after_b00..b02_*.png.
  - Wine prefix: /tmp/wine_anim. Its log is /tmp/wine_anim/drive_c/users/root/AppData/Local/NeonTide/log.txt.
- Memory gate: SP/car/gate.sh (waits for tools/memfree.sh >= 2000).

## 6. Next steps

1. When memory allows (one game at a time, never while my own compile runs), redo the AFTER Wine run:
   `sh SP/locotour/run.sh after && sh SP/locotour/montage.sh after`
   It takes about 30 min of real time under load.
   - Read `grep "locotour" SP/locotour/log_after.txt | grep -v "shot \|big slide"`.
   - Compare with log_before.txt: steps, passes, the passing body language events, peds cpu.
   - Put the before_bNN and after_bNN montages side by side (same peds).
2. Send the sign-off to the lead (SendMessage to main). Include:
   - the blobs and the field list (section 1);
   - the exact peds.cpp hunks and the game.h line (section 2);
   - the tables (section 4) with the in-game after numbers;
   - "anim snapshot-safe".
   Then call SubagentHandback.
3. Clean up:
   - delete the two loco_*.exe, SP/loco/lm_base, lm_work and at_final;
   - keep the PNGs;
   - convert any leftover BMPs.
4. Recommendations for the lead:
   - pedai turnTo runs at 6 rad/s even at full walking speed, which a gait cannot follow (AI work): slow the turn
     or the walk in sharp turns;
   - the AI agent could add a look feed for fights.
5. Later, coordinated by the lead: NPCs getting in through doors (taxi fares, drivers to parked cars, the prisoner
   through the rear door). That needs a GameWorld hook beside my door helpers in peds.cpp, once the AI agent's
   second set has landed.

Re-check commands:

- Harness: `sh SP/loco/final.sh` (rebuilds both locometers and anim_test, gated).
- One case: `SP/loco/lm_work turnwalk.135`. Trace one run: `LM_TRACE=12 SP/loco/lm_work turnwalk.135 2>trace.txt`.
- Syntax check of a test tree: `sh SP/car/gate.sh sh SP/locotour/check.sh SP/locotour/after -DLOCO_AFTER`.
