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

## 7. Update 2026-10-05 (after the reboot)

- The locomotion pass is MERGED: PR #26, main e9a7259. My 4 files equal HEAD there.
- The after Wine run finished (SP/locotour/log_after_run1.txt; montages SP/locotour/montage/after_b00..b07).
  - In game, slide per step: mean 14.9 -> 7.4 cm, p90 41.6 -> 14.3 cm.
  - Arm clearance at passes: mean 13.7 -> 20.0 cm.
- CPU, same machine: peds 1.83 (before rerun) -> 2.03 / 2.11 ms/frame (after, two runs).
  - Harness animator: +0.5 us per update.
  - Hunk 2's scan: est. 30-60 us/frame.
  - The lead asked for an every-other-frame scan in the next sign-off. The new hunk 2 is in
    SP/locotour/after2/src/game/peds.cpp ("Looked for every other frame").
- Sliding peds (instrumented run, SP/locotour/log_after_instr.txt): 61 of the 75 slides over 20 cm (90% of the
  distance) are group FOLLOWERS (pa.leader >= 0). Their slot-keeping steering in pedai.cpp (~line 990) has a 0.25 m
  dead zone, and they face the slot vector, which jitters at 2.5-4 rad/s. Reported to the lead for the AI agent.
- Curb-edge probes: SP/loco/patches/p14_edgeprobe.py, plus the single-edge restriction and the `near` -> `under`
  rename (`near` is a Windows macro). The work copy SP/loco/work holds it.
  - Harness: curb-up toe in the riser goes from -150 to -30 mm. There are some extra "overhang float" frames.
  - Test tree SP/locotour/after2 = after + edge probe + peds.cpp edge-probe insertions + every-other-frame scan.
  - Exe after2/bin/loco_after2.exe. Its run: `sh SP/locotour/run_after2.sh` -> log_after2.txt.
- Next: judge after2 in game against log_after_run1.txt (lowest sole, floats, slides, CPU). If it is good, apply
  to the shared tree:
  - animator.cpp and character.h from SP/loco/work;
  - peds.cpp: the hunk 2 change and the two edge-probe insertions from SP/locotour/after2/src/game/peds.cpp.
  Then sign off with the exact text.

## 8. Update 2026-10-05, evening

- Snapshot 28 (main 91a6aba) carries the every-other-frame passBy scan: peds.cpp 05509b0.
- Edge probes (p14): shelved by agreement. There was no measurable in-game gain at stop 2. The code is kept in
  patches/p14_edgeprobe.py and SP/loco/work_animator_p14b.cpp / work_character_p14b.h.
- The causes of the sliding peds went to the lead:
  - group followers' slot steering (pedai ~990);
  - the anchor arrival with no hysteresis (pedai ~1475);
  - position pushes that bypass the velocity (updatePeds' ped-ped separation, movePed's collider push).
- rootMove (approved), uncommitted in the shared tree on top of 91a6aba, signed off with these blobs (written -w):
  - animator.cpp 9ffc7f9b9305ff3dd44f38d8db5b942ba9abbe10;
  - character.h 5521db84c2b00843d30faf6695658c36733a0774;
  - peds.cpp 9b7e617ecce1cf95005c8a0a844bb55b0460e166;
  - game.h 5920a793612a8685d13c2076c4d97e42f8ab61f3.
  Patches: SP/loco/rootmove_91a6aba_{anim,game}.patch.
- Harness:
  - new scenarios push.stand / push.walk (pushed without a velocity), with *_velonly controls;
  - Person::animPos feeds AnimInput::rootMove like the game;
  - timing of the animator update printed at the end.
- In game: SP/locotour/log_after3.txt (scan + rootMove). All steps mean 7.42 -> 6.76, p90 14.3 -> 11.6; standing max
  143 -> 33.
- Waiting on: the snapshot of rootMove; the AI agent's follower and anchor fixes; then the NPC door-entry hook (the
  lead will say when).

## 9. Update 2026-10-05, night (main 111efa9)

- PRIORITY (coordinator): pass-by regression - companions at the elbow counted as passers. Fix in the SHARED tree's
  src/game/peds.cpp (uncommitted; base 111efa9): skip candidates under 0.3 m/s relative speed (rv2 < 0.09), skip own
  walking group (PedAI leader / leaderUid), weight eased in over 0.3-0.5 m/s. Blob (written -w):
  peds.cpp 91e6515e32feab8d7a1caa312ecb0105e562749c. Patch: SP/loco/passfix_111efa9.patch.
  - Harness (LM_PASS_OLD=1 = before): head-on / overtake metrics identical; abreast 0.6 m twist 22.8 -> 0 deg, side-step
    8.6 -> 0 cm; abreast 0.85 m 8.4 -> 0 deg; pace 0.6 (0.2 m/s apart) 22.6 -> 0 deg. Arms at 0.6 m without the lean
    overlap 6 cm (free arms); in game non-couple companions walk at 0.85 m (15.6 cm clearance), couples hold hands.
  - In game run 1 (SP/locotour/log_after_pb0_run1.txt vs log_after_pb1_run1.txt): walking-together lean ped-frames
    178 -> 31; close passes 38 -> 38, clearance min -2.0 -> -7.2, mean 18.5 -> 17.9, through 1 -> 4 (to explain: rerun
    with per-pass logging by walking group: SP/locotour/pb_rerun.sh -> log_after_pb0/1.txt, flag pass_reruns.done).
  - Then send the pass-fix sign-off (peds.cpp-only, blob + graft) to main.
- Stair gait (work copy SP/loco/work, = p16r + grabAxis; backups work_animator_p16*.cpp):
  - plan onto the nearest-fitting tread (multi-segment), learned strike lead, margins = 2 cm*scale + half a scan step,
    ascent may land on the forefoot (heel over the nose: ballProbe -> ground probed under the ball, forefoot pin, 30 ms
    ease, ball pivot), landing probe under the forefoot (0.4 L ahead of the middle), footL/R snapped to the planned tread
    at the plant, probe clamp 0.35 -> 0.5 in the animator and 0.3 -> 0.45 in peds.cpp, scan trigger also off the other
    foot's ground (> 10 cm).
  - Harness vs HEAD (head_full3.txt vs work_p16u.txt): up sunk3 59.7 -> 0.36%, down 18.6 -> 0%; up slide 2.97 -> 0.64
    (max 9.6 -> 1.6); down 1.56 -> 0.14 (max 7.1 -> 0.77); down floats 107 -> 7; up lifted 0.9 -> 3.2% (landing settle);
    curb up toe -150 -> -30 mm; anim_test ALL PASSED (SP/loco/at_st.log); timing within noise.
  - peds.cpp side: SP/loco/patches/p16_peds.py (clamp 0.45, scan block, grabAxis reset).
  - In-game stair test: --autoplay locostairs (SP/locotour/block_stairs.cpp, apply2.py), trees before_st / after_st,
    SP/locotour/st_all.sh (runs wait for pass_reruns.done).
- grabAxis (low priority, in the work copy): AnimInput::grabAxis (default up); horizontal = hands held while walking:
  the hand hangs, fingers down, palm square to the way (holdGrip axis = cross(-z, palm) right / cross(palm, -z) left;
  checked with SP/grip/grip_test.cpp).

## 10. Update 2026-10-06 morning (main cda29a1)

- Pass-by fix MERGED (PR #34): peds.cpp 7aaad255 = d3c2c3f + the going-by rule (lean only while someone among the
  candidates goes by at 0.3 m/s or more; selection as 111efa9). In game: passes unchanged (overlaps 1 -> 1, mins same),
  companion lean frames 178 -> 63. Logs SP/locotour/log_after_pb0/2/3.txt.
- Stair gait + grabAxis: blobs written -w (base cda29a1 / peds.cpp 7aaad255):
  animator.cpp d850a8fe6395713f4126a4033c72b23fab34e158, character.h 6e761ebe6a0d9df5bab1c6474c74380160fcb50d,
  peds.cpp e2bf946d1f18bc83658ef927736d5183213ef97a (7aaad255 + SP/loco/stairs_peds_7aaad25.patch).
  Anim patch: SP/loco/stairs_anim.patch (applies cleanly to the shared tree; not applied yet).
- In-game stair test: --autoplay locostairs (block_stairs.cpp). First comparison (before_st / after_st) had a lane bug
  (both directions in one lane: walkers deadlocked) - not usable for slides. Clean trees before_st2 / after_st2 (lanes
  fixed, per-frame trace of two walkers): runs scheduled by SP/locotour/orch6.sh after 08:30 UTC (memory), log orch6.log.
  The killed before_st2 run's log (HEAD trace) is log_before_st2_killed.txt.
- Never use pkill -f with a pattern that appears in your own command line (it killed my shell): kill by PID.

## 11. Update 2026-10-06 ~10:00 (main b907098) - stair gait SIGNED OFF (with grabAxis and startCarEntry)

- Shared tree (uncommitted) = b907098 + animator.cpp 14eea674, character.h 6e761ebe, peds.cpp 1df70f7a (on 7aaad255),
  gameworld.h 6890cafb (on a72a48c). For snapshot 38's peds.cpp b2436983: 5c6e2ff7. All blobs written -w.
  Patches: SP/loco/stairs_anim.patch, final_peds_cda29a1.patch, final_peds_b2436983.patch, final_gameworld_cda29a1.patch;
  peds.cpp hunks script SP/loco/patches/final_peds.py.
- Final animator change after the first in-game run: a swing stepping down a planned step probes under its heel until
  it clears the nose (descent scuffs 20 -> 2, metro-like stairs down slide max 18 -> 1.5 cm).
- In game (Calle Luna stair, SP/locotour/log_before_st2.txt vs log_after_st2.txt): up sunk3 58.8 -> 6.8%, down 29.0 ->
  2.9%, up slide 5.9 -> 2.0 cm/step, scuffs 75 -> 24 / 131 -> 31, peds cpu equal (2.16).
- Possible follow-up: toe roll into the riser at heel-off on ascent (most of the remaining 6.8%): the upAhead toe margin
  (0.02*scale extra) could go to ~0.04*scale.
- Test exes deleted. Harness binaries kept: SP/loco/lm_head, lm_work.

## 12. Update 2026-10-06 ~11:00 (main e4efe94, snapshot 39 queued) - toe-margin + deep-frames follow-up

- Shared tree peds.cpp now = 5c6e2ff (snapshot 38's AI look block inserted). animator.cpp 14eea674, character.h 6e761ebe,
  gameworld.h 6890cafb unchanged in the shared tree (the follow-up is still in the work copy).
- Work copy (SP/loco/work): t1 = upAhead toe allowance 0.045*scale (was 0.02); t2 = + re-plan when the predicted landing
  drifts 3 cm (scanLand[2] in character.h, carried with root motion; re-scan up to swingU 0.9 on stairs) + plantCorr ease
  timed to the landing (only while a stair landing is planned). Backups: work_animator_t2pre.cpp / work_character_t2pre.h.
- Harness: new scenario stairs17.crowd (brake to 55% every 2.2 s for 0.6 s + weave +-0.3 m/s). HEAD: up sunk3 62.8%,
  down 20.6%; s39/t1: 15.2 / 15.5%; t2: 7.7 / 6.6%. Every steady scenario identical t1 vs t2 (work_t1.txt vs work_t2b.txt).
  Remaining crowd failures = late re-plans (last 20-80 ms) the swing cannot reach, and landing errors > margin (4.5 cm).
  Tried a sticky re-plan cost (from the current shift): no gain, reverted.
- Trace tools: LM_TRACE=<run> (runs numbered within the filter), SCAN lines (locosim.h gScanLog), landerr.py, stance.py,
  tview.py in SP/loco. mk.sh fixed (it wrote lm_work.new into the repo root once: deleted).
- In game after_st3 (toe fix only + deep-foot log) built 10:48, run via orch7 -> log_after_st3.txt.
- 11:40 coordinator: snapshot 39 MERGED (PR #39, main b49bc7f): HEAD has animator 14eea674, character.h 6e761ebe, peds.cpp
  5c6e2ff7, gameworld.h 6890cafb (the shared tree equals HEAD for these). Base the follow-up on b49bc7f; sign off as usual.
- In game after_st3 (toe allowance only; deterministic run): up sunk3 6.8 -> 3.5%, lowest -169 -> -184; down identical to
  after_st2 (2.9%, -275.0 mm). The -275 frame (deep log): walker 180 dn, a 29.38 (mid flight 5), the plant frame
  (age 0.03), footZ -0.275 = between treads (no plan snap; the swinging foot's near probe 'just ahead' had pulled footZ
  toward the next lower tread), ball/toe 9 cm into their tread, heel over the upper tread (-27.5). Remaining in-game
  ascent sinks of the traced walkers: late-stance toe roll into the next riser (2-3 frames), deep log: also whole stances
  planted too far forward. In-game walkers are often held back by ped-ped separation (position-only) while their
  velocity (= the gait's speed) stays: feet dragged -> quick steps (low arc through risers) -> deep frames.
- Harness: +stairs17.blocked (held to 20% for 0.5 s every 2.6 s, velocity unchanged), crowd/blocked 4x sample, LM_HZ=30
  (the game's fixed autotest step), quick_steps + planted_below_a_riser_frames metrics, LM_ACTUAL (gait at the actual
  speed: worse, dropped).
- Candidate t4 (clean: SP/loco/clean_animator_t4.cpp / clean_character_t4.h) = toe allowance 0.045 + re-plan on 3 cm drift
  + landing-timed ease + plantSlack (a tread-shifted planted foot may stray its shift further before a quick step) +
  quick steps on stairs probe their landing and keep over the higher ground until 60% (no riser crossing).
  30 Hz: crowd up/down sunk3 t1 12.5/18.1 -> 5.4/8.4%, lowest -257/-170 -> -170/-170; blocked t1 6.7/12.1 -> 4.0/10.4%,
  lowest -307/-340 -> -182/-170, quick steps 38/36 -> 28/26; steady stairs17 up s39 1.69% -> 0.13% (toe roll).
  No other scenario changes (60 and 30 Hz).
- In game after_st4 = t4 + test-only plant diagnostics (dbgPlantKind/PlanLand/FzPre in its character.h/animator.cpp, deep
  line prints kind/plan/fzPre/stepT; trace has heel/toe along-positions + plan fields). orch8 -> log_after_st4.txt.
- 12:15 coordinator: the four changes are fine; sign off after after_st4 confirms. Asked where the ped-ped separation
  lives: replied peds.cpp GameWorld::updatePeds (line 1114 at b49bc7f; position-only push, p.vel untouched) + ai.cpp
  aiWalkRound stuck test (p.vel < 0.15, never true when held by separation; BRAIN_GOTO has no ped avoidance).
  Proposed: separation removes the closing part of the relative velocity (as movePed does for static colliders), and
  GOTO walkers ease off behind slower ones (AI agent). Not the anim feed (LM_ACTUAL experiment was worse). If it comes
  to me: a LATER follow-up, not this one.
- Candidate blobs (not yet in shared tree): animator.cpp 7c464f6f, character.h ba7b4ef3 (= clean_*_t4).
- 12:20 coordinator: Change 1 is MINE, as the follow-up AFTER the toe-margin sign-off: in updatePeds' ped-ped
  separation remove the closing part of the pair's relative velocity, split with the position-push weights (like movePed
  for static colliders). Base on HEAD's peds.cpp at that point (AI set 16 adds a disjoint couple-block line there; the
  coordinator grafts). Extra checks: (1) give the test exe path in the sign-off so the AI agent can run crowded,
  crossing, walkby, life, busrun on it (watch queues, crossings, companions abreast); (2) check the player (pushed by /
  pushing into a crowd at 0.25 weight): melee smoke or a short first-person walk into a group. Change 2 -> AI agent.

## 13. WIND-DOWN 2026-10-06 ~13:40 (main b49bc7f) - stair follow-up SIGNED OFF; nothing running

### Signed off (sent to main, "anim snapshot-safe", base b49bc7f; the shared tree holds the files uncommitted)
- src/anim/animator.cpp = 7c464f6f220be47217f4c43d0b3ec6f8db4cd65e (b49bc7f 14eea674)
- src/anim/character.h  = ba7b4ef378c835511f393cefd3abc2540b5b8eb1 (b49bc7f 6e761ebe; additive: scanLand[2], plantSlack[2])
- blobs written -w; patch SP/loco/followup_t4_b49bc7f.patch; clean copies SP/loco/clean_animator_t4.cpp / clean_character_t4.h
- Content: (1) ascent toe allowance 0.045*scale; (2) re-fit a swing's landing when its predicted spot moved >= 3 cm (up to
  swingU 0.9; scanLand carried with root motion) + the plantCorr ease timed to the landing while a stair landing is planned;
  (3) plantSlack: a tread-shifted planted foot may stray |shift| further before a quick step; (4) quick steps on stairs
  probe their landing and keep over the higher ground until stepT 0.6.
- Evidence: harness 60/30 Hz (SP/loco/fin_s39_60/30.txt vs fin_t4_60/30.txt; only stair metrics change), anim_test ALL
  PASSED (SP/loco/at_t4.log), in game log_after_st4.txt vs log_after_st2/st3 (up sunk3 6.8 -> 3.1%, down 2.9 -> 1.2%, down
  lowest -275 -> -169, down slide max 56 -> 11; up lowest -233 = a no-plan plant at a flight's top step; up slide max 43).
- The -275 mm trace: no-plan descent plant, footZ -0.275 between treads (the 15 cm-ahead near probe), heel over the upper
  tread. Same mechanism gave -233 up in after_st4. Deeper frames also from quick steps of walkers held by the separation.

### Unfinished work (in order of priority) and where it lives
1. CHANGE 1 (assigned to me by the coordinator, NOT STARTED - do it as the next follow-up when work resumes): in
   src/game/peds.cpp GameWorld::updatePeds ped-ped separation (line ~1114 at b49bc7f), remove the closing part of the
   pair's relative velocity split with the position-push weights (like movePed's static colliders, peds.cpp:332). Base it
   on HEAD's peds.cpp at that time (AI set 16 adds a disjoint couple-block line there; the coordinator grafts).
   - Ready: patch script SP/loco/patches/sep_vel.py (vc = dot(b.vel - a.vel, n); if vc < 0: a.vel += n*vc*wa,
     b.vel -= n*vc*wb), preview SP/loco/peds_head_sep.cpp.
   - Test trees prepared (UNBUILT): SP/locotour/push0 (b49bc7f + t4 anim + test blocks via apply3.py) and push1 (= push0 +
     sep_vel). Rebuild them from the then-HEAD if HEAD moved. Scripts: build.sh <tree>, run_ap.sh <tree> <autoplay> <tag>,
     orch9.sh (the chain: check, build, locopush, melee --autoduration 20, locostairs for both; it was killed unused).
   - New test --autoplay locopush (SP/locotour/block_push.cpp, inserted by apply3.py): Police HQ sidewalk 13:00, population
     off, a knot of 6 standing 5 m ahead; the player walks through (stick 0.44), back, runs through (sprint), then stands
     while 4 walk at it; per phase: actual vs asked speed while touching, passage time, knot shove, player shove.
   - Required checks (coordinator): (a) give the test exe path in the sign-off so the AI agent can run crowded, crossing,
     walkby, life, busrun on it (watch queues, crossings, companions abreast) - build a clean HEAD + change exe for that;
     (b) the player pushed by / pushing into a crowd must feel the same at 0.25 weight (locopush + melee smoke);
     (c) harness stairs17.blocked (LM_HZ=30 too) and locostairs in game.
   - Expected effect (analysis): movePed re-accelerates (11 m/s2, 0.37 m/s per 30 Hz frame), so a walker pressing into a
     standing person settles near 0.74 m/s gait speed at 30 Hz (0.37 at 60 Hz) instead of 1.4; behind a slower walker it
     changes little. The AI-side fix (CHANGE 2: BRAIN_GOTO walkers easing off behind slower ones; aiWalkRound's stuck test
     uses p.vel) is routed to the AI agent. Do NOT feed the gait the actual speed (LM_ACTUAL harness test: much worse).
2. In-game ascent landings 5-10 cm further on than planned (log_after_st4 trace of walker 175, positions u heel/toe): at the
   30 Hz step the pose touches down (held re-plant, kind 3) before the gait strike, so a pull-back shift is incomplete, then
   the toe roll takes the toe into the riser. Idea: finish the shift by the touch-down (tLand to swingU ~0.85, or from the
   pose foot's height over its landing tread). A plain 0.5*spd*dt forward bias in the plan HURT crowd descents - don't.
3. No-plan plants on stairs (kind 2) keep a lagging/leading footZ (the -275 / -233 frames). Idea: at a no-plan gait plant
   with stairSeen < 1.5, footZ = max(footZ, groundAhead) (the landing probe), harness-check at 60/30 Hz first.

### How to resume
- Harness (SP/loco): lm_head (111efa9 anim; build with -DLM_HEAD; run with LM_PROBE_CLAMP=0.3), lm_s39 (b49bc7f anim,
  tree s39tree), lm_dbg (= signed-off t4 + debug-only fields dbgDrift/dbgPred/dbgPlantKind, tree SP/loco/work).
  Build: sh mk.sh <tree> <out> (gated host compile; output now absolute). Env: LM_HZ=30, LM_TRACE=<run index within the
  filter>, LM_KINDS=1 (stances by plant kind), LM_STEPS=1, LM_ACTUAL (experiment), LM_PASS_OLD. Scenarios: stairs.walk_1.3,
  stairs17.walk, stairs17.crowd (4x), stairs17.blocked (4x). Tools: cmp.py, landerr.py (planned vs actual heel landing,
  needs SCAN lines), stance.py, tview.py. anim_test tree: SP/loco/attree (b49bc7f + t4).
- In game (SP/locotour): trees after_st2/st3/st4 (sources; exes deleted), push0/push1 (unbuilt). block_stairs.cpp (trace of 2
  walkers with heel/toe positions + plan fields, deep-foot log with plant kind via test-only dbg fields in the tree's anim
  files), apply2.py (locotour+locostairs) / apply3.py (+locopush). build.sh <tree>; run_st.sh <tree> (locostairs, log ->
  log_<tree>.txt); run_ap.sh; gstance.py (traced stances). Logs: log_before_st2 (111efa9), log_after_st2 (b49bc7f),
  log_after_st3 (toe only), log_after_st4 (signed-off version + diagnostics).
- Etiquette: run.sh/build.sh hold while /tmp/neontide_lead_wants is fresh; host compiles via SP/car/gate.sh (memfree >=
  2000); detached chains with setsid nohup; kill by PID only; delete test exes and BMPs after use.
- State at wind-down: no games, builds or chains of mine running (orch7/8 finished, orch9 killed before building).

### Status of the sign-off and the two open items (coordinator, 13:45)
- Verified by main: animator.cpp 7c464f6f (on 14eea674), character.h ba7b4ef3 (on 6e761ebe). Goes into SNAPSHOT 41 with
  faces batch 7 (face.cpp, anim_test.cpp, preview.cpp: no overlap). 41 starts once snapshot 40 merges. After the merge,
  HEAD's anim files = these blobs: check `git hash-object` before any further anim work.
- Open item A, named in snapshot 41's commit message: the -233 mm SINGLE ASCENT FRAME (log_after_st4 deep log: walker 178
  up, a 20.02, plant kind 2 = no plan, footZ -0.010 under a +0.17 tread, then -23.3 the next frame). Cause: a plant without
  a tread plan near a flight's top step keeps the swinging foot's lagging near-probe height. Same mechanism as the old
  -275 mm descent frame; 111efa9 had -238 up. Fix to try: unfinished item 3 above (footZ = max(footZ, groundAhead) at a
  no-plan gait plant on stairs). Also find out why the plan was missing there (scan saw the landing? |gl - gFoot| >= 0.4?
  shift > 0.2*scale?). The test tree's deep log prints kind/plan/fzPre for this.
- Open item B, named in snapshot 41's commit message: the 43 cm ASCENT SLIDE MAX in after_st4 (one step; b49bc7f had 29.5).
  It was not on a traced walker (175/176), so it is not attributed yet. Next: log every slide over 20 cm in block_stairs
  (uid, dir, foot, its plant kind, stepT, along position, speed vs actual speed) and rerun. Likely candidates: a quick step
  whose heel crossed a riser counted as contact (seen in the harness), or a walker held back by the separation.
  CHANGE 1 may reduce it, so check B again with the change 1 runs.
- CHANGE 1 handoff details (also above): harness stairs17.blocked (+ LM_HZ=30) for before/after; in game locostairs +
  locopush + melee smoke; AI crowd tests (crowded, crossing, walkby, life, busrun) are the AI agent's. The sign-off gives
  the AI agent the path of a clean exe (HEAD + the change) to run them on. Watch queues, crossings and companions
  walking abreast. CHANGE 2 (BRAIN_GOTO walkers ease off behind slower ones) is the AI agent's.

## 14. RESUMED 2026-10-12 (main 894e7fb: my stair follow-up merged in PR #41, AI sets 10-13 in PR #42)
- HEAD peds.cpp = 5c6e2ff (mine), animator 7c464f6f, character.h ba7b4ef3 - the shared tree equals HEAD for my files.
- Task now: CHANGE 1 (separation velocity) per section 13; checks: harness stairs17.blocked + full suite, locostairs,
  locopush, melee smoke; sign-off with a clean exe path (HEAD + change) for the AI agent's crowded/crossing/walkby/life/
  busrun runs. Then, if room: the in-game ascent overshoot (item 2) and no-plan plants (item 3).
- Disk tight (~7.5 GB free): keep exes and shots few.
- Harness emulation of CHANGE 1 (LM_SEPVEL=1; locometer's stairs17.blocked now models the one ahead by speed: Ctl.blockV,
  control() caps the along-walk motion at it; the separation then takes half the closing speed off the velocity after the
  update). 60 Hz blocked: quick steps 29/27 -> 13/14, slide max 8.4/14.9 -> 6.8/7.4 cm, scuffs up 56 -> 40 (down 55 -> 65),
  but sunk3 up 1.4 -> 6.3%, down 9.3 -> 11.0%, one descent frame -270 mm. 30 Hz: quick steps 28/26 -> 21/22, slides down,
  sunk3 up 4.0 -> 4.7, down 10.4 -> 9.9, lowest down -202. Mixed on stairs: the gait now slows while held (0.62 m/s at 60
  Hz, 1.0 at 30 Hz, root at 0.26) and the stair plans miss more.
- Tried: landing prediction with the root's actual speed (A.rootSpd EMA 0.1 s, LM_ROOTSPD): blocked descents 9.3 -> 1.4%
  but crowd descents 5.9 -> 8.7% (60 Hz) and a 59 cm crowd slide at 30 Hz. Dropped (work copy restored).
- 14:00 in-game chain for CHANGE 1: SP/locotour/orch10.sh (detached; log orch10.log): trees rebuilt from 894e7fb:
  push0 (HEAD + test blocks), push1 (push0 + sep_vel.py), sepclean (HEAD + sep_vel.py = the clean exe for the AI agent:
  SP/locotour/sepclean/bin/loco_sepclean.exe once built). Runs locopush + melee (--autoduration 20) + locostairs on push0
  and push1; deletes the push0/push1 exes at the end (keeps sepclean's).
- Full harness suite with/without LM_SEPVEL: only stairs17.blocked metrics differ (by construction: the animator is
  unchanged; the separation is only modelled there).
- Work copy experiments (env-gated, DEBUG): LM_NOPLAN (no-plan gait plant on stairs: footZ = max(footZ, groundAhead)),
  LM_NOSCAN (stairs detected but no tread plans, to exercise no-plan plants).
- In-game data (after_st4 trace, planned ascent stances, risers modelled at flightStart + j*0.29 + 0.02 collision nose):
  toe tip to the riser ahead at the plant median 7.4 cm (plan target ~9), p10 4.3; toe roll median 3.8, p90 4.5 cm.
- Item 2 candidate: toe clearance 0.055*scale + 0.5*speed*dt (patches/toe2.py; harness 30 Hz crowd up 5.45 -> 4.1%,
  lifted lower; 60 Hz about equal). Item 3 candidate: patches/noplan.py (harness: ascent no-plan below-riser frames cut,
  nothing else changes). Test tree SP/locotour/st5 = 894e7fb + change 1 + item 3 + item 2 + planned-heel trace (pU);
  orch11.sh builds it after orch10's builds and runs locostairs after orch10.
- 17:00 in game (locopush, push0 = HEAD vs push1 = HEAD + change 1 as specified): walking into the standing knot the
  player now gets through at 0.66 m/s instead of 0.20 (velocity 1.36 -> 1.01 while touching) and the knot is shoved 2.98 m
  instead of 0.77; running into it the player's velocity drops 6.9 -> 2.4 while touching (actual 2.94 -> 2.01 m/s), the
  knot shoved 5.1 m (6.2 before); four walking at the standing player: shoved 5.0 m both. => the player's feel CHANGES.
  Variant sep_vel2.py (no velocity change in pairs with the player): push2 / sepclean3 (61a3b7b) via orch13.sh.
- orch12 was killed by the faces agent at 16:52 (by mistake); its build.sh sepclean2 survived and kept compiling.
- 17:10 coordinator: GO WITH THE VARIANT (sep_vel2: velocity only between two non-player peds). Sign off once push2's
  locopush + melee match HEAD and locostairs is in; include the clean exe path SP/locotour/sepclean3/bin/
  loco_sepclean3.exe (61a3b7b + variant) for the AI agent's crowded/crossing/walkby/life/busrun runs. Items 2-3 (st5) =
  their own sign-off afterwards. Variant peds.cpp blob = 4568ab10f13cb2ac362e73fb3102136704fd0552.
- LESSON: two of my games ran at once (orch13's push2 melee and orch11's st5 locostairs) and SHARED /tmp/wine_anim's
  log.txt: push2's melee log lost its first 9 s (kept as log_push2_melee_clobbered.txt). Rerun alone via orch14.sh.
  Keep ONE game of mine at a time (chain the orchestrators).
- In game locostairs (push0 = HEAD vs push1 = HEAD + change 1): up slide max 42.96 -> 8.26 cm, mean 1.91 -> 1.62, lowest
  -233 -> -169; down slide max 11.3 -> 8.1, p90 4.46 -> 3.16; sunk3 up 3.0 -> 3.7%, down 1.2 -> 1.8%; scuffs 26/29 ->
  29/32; trips 26 -> 27. locopush: push2 (variant) = push0 in phases 1-3 exactly; phase 4 within noise (11.67 vs 12.00 s
  touching, shoved 5.01 vs 5.00 m).

## 15. 2026-10-12 evening: both sign-offs verified; now the in-game ascent overshoot (landing past the plan)
- Coordinator: Change 1 VERIFIED (peds.cpp 4568ab10 on 5c6e2ff; merged with AI sets 14-16 = 52a49711; AI agent runs
  crowded/crossing/walkby on loco_sepclean3.exe, then a snapshot). Items 2-3 VERIFIED (animator 0e56a2c1 on 7c464f6f; next
  snapshot with AI sets 14-16 + night phase A after PR #45). Keep SP/locotour/sepclean3/bin/loco_sepclean3.exe until the
  AI agent is done.
- Next task (my choice, told main): the ascent overshoot; later maybe the hand-in-hand clasp finger pose (couples' hands are
  two fists via holdGrip in animator.cpp "a hand holding on to a point"; peds.cpp sets grabTarget/grabAxis; low value at
  street distance).
- CAUSE FOUND: strikeLead (learned strike lead, used by every landing prediction: the scan's lm and the landing probe)
  is read at the plant update, which comes after the clip's strike (ph 0.004..0.06), when the stance foot has already gone
  back by the body's travel ph*cycle -> the lead reads short and every plan falls short of the actual landing by
  E[ph]*cycle: 2.5 cm at 30 Hz, ~1.2 at 60, up to 7 cm for walkers whose plant comes 2 updates late (in game walker 175:
  errors grow 2.4 -> 7.8 cm as the EMA settles; in-game plant phases up 0.03-0.06, down 0.00-0.03).
  In game (log_st5, tool SP/loco/sl/gland.py / gland2.py): up median 2.9 p90 6.5; dn median 1.7 p90 3.2 (cm).
- Fix: obs += ph (stance plants). Harness (SP/loco/sl: tree = shared animator + debug + env knobs LM_SLFIX, LM_HEELBACK,
  LM_RATIO, LM_TOEALLOW, LM_TOEDT; lm_sl built with -DLM_LAND: landing-error metrics land_gait/held_err_*, SINK/LIFT logs
  LM_SINKLOG/LM_LIFTLOG, LM_SEEDOFF seeds; seeds.sh + sstat.py = 8 seed sets mean+-SE): landing error median -> ~0.2 cm.
  BUT alone it worsens perturbed walking (the bias had hidden short landings): blocked up 30 Hz 4.15 -> 7.93% sunk3.
- Candidate (= patches/landpred.py; clean copies sl/cand_animator.cpp, sl/cand_character.h): (1) strike lead + ph;
  (2) rootRatio (character.h, additive): EMA 0.05 s of the root's actual move along md over spd*dt, clamped 0..1, scales
  the gait's remaining travel in both predictions (a held-back body lands short); (3) descent heel margin lo = r + mg +
  0.04*scale; (4) toe dt term kept (re-explained: re-fit at most once an update). sunk3 % (8 seeds, base -> cand):
  60 Hz crowd up 2.97/2.93, dn 4.85/2.69, blocked up 2.23/~1.1, dn 8.35/~0.5, blocked+SEPVEL up 6.13/~3.2, dn 11.87/~3.3;
  30 Hz crowd up 3.40/3.61, dn 7.10/5.37, blocked up 4.15/~2.3, dn 9.82/~1.7, blkSV up 3.10/~3.0, dn 8.27/~1.8.
  Full suite: no non-stair changes (hill10 down slide p90 2.37 -> 2.11 at 30 Hz). Side effect: descents' 'lifted' rises
  (2.4 -> 6.8% at 60 Hz) = the ball just past the nose (< 2.5 cm) at the upper tread's height (benign).
  anim_test ALL PASSED (SP/loco/sl/at_cand.log, tree SP/loco/attree2).
- In game: SP/locotour/st6 = st5 + landpred.py; orch15.sh (detached) builds and runs locostairs -> log_st6.txt; compare with
  log_st5.txt (gland.py for landing errors + the locostairs summary lines).
- 22:35 st6 in game (log_st6a.txt = first run; st5 -> st6): landing past the plan up median 2.9 -> 0.6, p90 6.5 -> 1.7;
  down 1.7 -> 0.3, p90 3.2 -> 1.4 (traced walkers; -57 outlier in both = a mis-associated plan). locostairs: up sunk3
  1.7 -> 1.1%, sunk1 2.1 -> 1.4, lifted 0.6 -> 0.5, scuffs 22 -> 20, slide mean 1.66 -> 1.73, p90 3.70 -> 3.58, MAX 8.27 ->
  30.0 (one step, unattributed); down sunk3 1.8 -> 1.7, lifted 5.2 -> 6.1 (ball on the nose), scuffs 33 -> 28, slides same;
  trips 27 both; across a riser 79 -> 87%. Harness: >15 cm slides over 8 seeds not more frequent (60 Hz 3/4/9 -> 1/4/3,
  30 Hz 0/9/6 -> 1/6/3 for crowd/blocked/blocked+SEPVEL).
- Coordinator 22:2x: Change 1 passed the AI crowd tests -> snapshot 45 (peds.cpp 52a49711) with items 2-3, AI sets 14-16,
  night phase A. sepclean3 exe deleted. Interim status sent to main.
- orch16.sh: st6 rebuilt with a test-only 'locostairs bigslide' log (contacts sliding > 15 cm: biggest frame, planted /
  hold / stepT / age / rootRatio / v / a) and rerun (deterministic) -> log_st6.txt. Candidate blobs: animator 18c81532
  (on 0e56a2c1), character.h 6910e2c3 (on ba7b4ef3) - NOT yet in the shared tree (snapshot 45 grafts 0e56a2c1).
- 23:45 rerun (log_st6.txt) identical to st6a (deterministic). The only slide > 15 cm: 'bigslide 180 up foot 0: 30.0 cm
  over 18 frames | biggest 7.8 cm at frame 9: planted 0 hold 0 stepT 0.63 rootRatio 0.87 v 0.68' = a quick step dragging
  its toe (same family as HEAD's 43 cm; harness >15 cm steps 60 Hz 16 -> 8, 30 Hz 15 -> 10 over 8 seeds).
- SIGNED OFF (sent to main ~23:50): animator.cpp 18c815320c415121d2101a1910bc85a93021a56e (on 0e56a2c1, +19/-8),
  character.h 6910e2c334bde316bff56343a04ea80dd5938e8a (on ba7b4ef3, +1 rootRatio); blobs -w; patch
  SP/loco/landpred_0e56a2c1.patch; clean copies SP/loco/clean_animator_landpred.cpp / clean_character_landpred.h.
  NOT applied to the shared tree (snapshot 45 grafts 0e56a2c1) - waiting for main to say when.
- Next: the hand-in-hand clasp (bounded). Test: --autoplay couple (AI agent's close-up on a Calle Luna sidewalk;
  app.cpp ~701 / ~2513). Code: animator.cpp hold block ("a hand holding on to a point", ga.z < 0.7 branch, holdGrip
  fingers 0.7 thumb 0.6, grip centre target - palm*0.035) + peds.cpp couple block (~893: target = middle + 8 cm towards
  the other, y +0.05, z 0.93) = AI agent's lines -> send the peds.cpp hunk to main, don't edit it in the tree.
- 00:0x coordinator: landing predictions ACCEPTED for snapshot 46 (with night phase B + AI taxi fix). Snapshot 45 committed
  = HEAD c1af87a (animator 0e56a2c1, character.h ba7b4ef3, peds.cpp 52a49711 = mine + AI grabAxis line). Applied the patch
  to the shared tree with the guard: shared animator.cpp = 18c81532, character.h = 6910e2c3. Shared peds.cpp is still
  4568ab10 (HEAD minus the grabAxis line) - asked main whether to restore HEAD's blob.
- Clasp: GO (bounded). peds.cpp couple-block hunk goes to main as a patch/blob (the AI agent owns those lines).
- Shared peds.cpp restored to HEAD's 52a49711 (guarded on 4568ab10; main asked). Shared tree now differs from HEAD only by
  my animator.cpp 18c81532 + character.h 6910e2c3 (landing predictions, snapshot 46).
- Clasp test tree: SP/locotour/clasp1 = git archive c1af87a + shared animator/character (landpred) + peds.cpp 52a49711.
- Clasp attempt 1 (orch17.sh, detached; log orch17.log): clasp1 = clasp0 + patches in SP/loco/clasp: clasp_anim.py (hold
  branch: axis turned about the palm by +0.6 rad for either hand -> right hand's fingers down-forward, left's down-back;
  curl 0.85 instead of 0.7; env CLASP_ANG / CLASP_CURL when patching), clasp_peds.py (couple point 0.08 -> 0.05 past the
  middle; CLASP_PAST), clasp_cam.py (TEST ONLY: --autoplay couple camera 0.85 m from the joined hands, behind / in front by
  turns, FOV 40). clasp0 = HEAD c1af87a + landpred + the camera only. QUICK builds (bld_cl.sh), runs run_cl.sh (1280x720,
  shots -> PNG in shots_clasp0/1, BMPs deleted).
- Clasp RESULT (01:07): before/after close-ups (same autoplay frames, deterministic AI): before = two hooked hands side by
  side; after = one clasp, each hand round the other (SP/loco/clasp/cmp_00.png, cmp_02.png; shots in shots_clasp0/1 PNG).
  Small dark intersection line at one wrist in some frames (minor). clasp0 run stopped early by PID (enough shots).
  Blobs (-w): animator.cpp 5464ab3ac1a1145f7b7165c25138ae92b8f11ce4 (on 18c81532, +7/-3: crossed by 0.6 rad about the
  palm, curl 0.85), peds.cpp ae083442abef4e57321af3e394cb6d9549036fa5 (on HEAD 52a49711, +4/-3: point 0.05 past the
  middle). Patches SP/loco/clasp/clasp_animator_18c81532.patch, clasp_peds_52a49711.patch. The two halves belong
  together (alone, the crossed hands would overlap 3 cm more). anim_test on attree3 (clasp animator) pending.
- 01:25 CLASP SIGNED OFF to main (anim_test ALL PASSED on attree3, SP/loco/sl/at_clasp.log): animator.cpp 5464ab3a (on
  18c81532) + peds.cpp ae083442 (on 52a49711, the AI agent's lines - main coordinates). Neither in the shared tree; offered
  to apply the animator half with a guard on 18c81532 when main says.
- Pruned superseded test trees (after_st2/3/4, before_st2, push0/1/2, sepclean/2/3); kept st5, st6 (stairs A/B), clasp0/1
  (clasp before/after; exes deleted). Logs and shots kept.

### State at this point (for a resume)
- Shared tree: animator.cpp 18c81532 + character.h 6910e2c3 (landing predictions, accepted for snapshot 46); peds.cpp =
  HEAD 52a49711. Nothing of mine running.
- Open (only if main asks): apply the clasp's animator half (guard 18c81532 -> 5464ab3a); possible clasp polish (the thin
  intersection line at a wrist: a slightly smaller angle 0.5 or point 0.055 - env CLASP_ANG / CLASP_PAST in the patch
  scripts); quick steps on stairs dragging the toe (the 30 cm in-game slide, walker 180: lift relative to the lowest sole
  over the treads it crosses).

## 16. 2026-10-13 ~01:30: clasp accepted (snapshot 47), both halves applied; next = quick-step toe drag on stairs
- Main: landing predictions in snapshot 46 (checks running); clasp accepted for snapshot 47 (with AI taxi/events fixes).
  Applied with guards: shared animator.cpp 18c81532 -> 5464ab3a, peds.cpp 52a49711 -> ae083442 (character.h 6910e2c3).
- Next (main, in order): (a) the quick-step toe drag that gave the 30 cm in-game up slide (walker 180: a quick step at
  stepT 0.63 dragging its toe over the treads); (b) anything in the locomotion tour that reads worse. Bounded attempt,
  usual sign-off.
- (a) quick-step toe drag, harness (SP/loco/qs: tree = shared 5464ab3a + debug + env knobs LM_QSHOLD, LM_HOLDDZ, LM_QSBALL,
  LM_QSDROP, LM_QSSNAP; locometer LM_QSLOG / LM_FLOATLOG logs; qrun.sh + qstat.py / qfloat.py / qsum.py / qsphase.py):
  quick steps on stairs = mostly the blocked case's BACKWARD steps on ascents (the planted foot left 30-50 cm ahead of the
  pose's footprint); the drag is late in the step (u >= 0.8: the foot drops to the landing's ground at u 0.6 while its
  trailing toe is still over the higher tread) plus early frames of the n=0 update (other foot just turned planted from a
  held re-plant: the root's vertical move extrapolated from rootVz). Candidate (patches/qsdrag.py; clean
  SP/loco/qs/clean/animator_qs.cpp = blob d2b3c98a on 5464ab3a, +10/-8, animator only): hold the higher ground until 0.85;
  plantedPrev/held include footHold; at a quick step's end on stairs footZ = the landing probe (groundRaw). 8 seeds:
  qs drag 30 Hz blocked 1037 -> 239 cm, blocked+SV 884 -> 262; 60 Hz 1248 -> 135, 578 -> 156; sunk3 / floats = base
  (without the snap: floats up to 34-50 cm for a frame or two, sunk3 lower only by those frames). Full suite: blocked up
  slide max 8.4 -> 1.8 (60), 17.7 -> 9.6 (30); nothing else. Tried and dropped: ball probe for the landing (worse),
  landing-timed descent (no gain). Remaining: a quick step's landing can straddle a riser (no tread fit for quick steps).
- In game: st7 = st6 + qsdrag (orch18.sh) -> log_st7.txt vs log_st6.txt (bigslide lines, locostairs summary).
- 02:15 worker restart (my process and Bash waiters lost; setsid chains survived). HEAD = 67e6159 (snapshot 46, with the
  landing predictions: HEAD animator 18c81532, character.h 6910e2c3). Shared tree: animator 5464ab3a + peds ae083442
  (the clasp, staged for snapshot 47). orch18 (st7 build + locostairs) and orch19 (then locotour on the same build)
  still running; waiter re-armed. Quick-step candidate blob d2b3c98a (on 5464ab3a; patch SP/loco/qs/qsdrag_5464ab3a.patch,
  script patches/qsdrag.py), anim_test ALL PASSED (SP/loco/sl/at_qs.log).
- In-game st7 (02:42 start): the 30 cm bigslide of walker 180 (t 489) is IDENTICAL to st6 -> not the level-change drag
  my patch fixes (the patch is active: other quick steps differ). orch19 (locotour) killed before it started; orch20.sh
  builds st7b (= st7 + uid 180 traced, trace cap 9000), runs its locostairs after orch18 (log_st7b.txt), then the locotour
  on st7/bin/loco_st7t.exe (log_st7t.txt).
- (b) notes: harness turnwalk 90/135/180 at 1.4 m/s: planted feet hover 4-16 cm (plantCorr 0.43-0.71 m at 6 rad/s turns,
  pivoting on the ball, leg out of reach while the other foot is mid-swing - the quick step needs the other foot down).
  In-game deep stances of walker 180 at x 0.37-0.48 (off the stair's centre line): ball/toe over the higher tread.
- 03:22 main: HOLD game launches until 03:55 or go (AI knock run gates PR #46). Killed my pending st7b run chain (orch20,
  run_st.sh, run.sh at the launch lock) by PID before launch. Kept st7b/bin/loco_st7b.exe and st7/bin/loco_st7t.exe for
  later: st7b locostairs (walker 180 trace), then the locotour on loco_st7t.exe.
- (b) FOUND: stopping on a flight. New harness scenario stairs17.stops (every 4.5 s a 2 s full stop on the flight): sunk3
  up 33.5%, down 20.6% (vs ~3% walking), slide max 76 cm, ~97 quick steps per direction; 189 of 219 quick steps with
  stairSeen >= 1.5 (the stair state lapses after 1.5 s standing -> quick steps after a stop use flat logic -> drags; likely
  walker 180's in-game 30 cm). Causes: standing steps (kind 4) to the flat idle stance land across risers; the abrupt stop's
  last swing lands +-12-15 cm off its plan (kind 1 stopped); standing plants (kind 5) unfitted.
  Knobs in SP/loco/qs/tree: LM_SEENWALK (stairSeen advances only while walking), LM_STANDD=k (+LM_STANDLVL: when the two
  feet's grounds differ > 5 cm, standing steps need k x the distance), LM_STANDLIM (standing leg-reach limit then).
  Seeds (srun.sh/sstat2.py, 4 seeds): seen alone: slide max 76 -> 33 (sunk unchanged); l6m (SEENWALK + STANDD 6 + LVL +
  STANDLIM 0.6): sunk up 33.5 -> 16.7%, down 20.6 -> 6.2% (30 Hz); down slides p90 3.9 -> 8.0 (worse), lifted up 0.5 ->
  2.5%. Full suite: only stairs17.stops changes. Not clean enough to sign off as is.
- 04:05 walker-180 TRACE (log_st7b.txt; deterministic): the 30 cm slide = a quick step of foot 0 (from a forefoot stance
  on the 0.34 tread, walker slowed to 0.7 m/s) while foot 1 SWINGS -> no foot held -> dzRoot extrapolated from rootVz
  (clamped 5 cm/update) for 3 updates while the root did not move vertically -> foot 0's ground sank 15 cm, ball/toe 3-10 cm
  into the tread while moving = the drag. Not the level-change case (so qsdrag v1 left it unchanged).
- Revised (a) = patches/qsdrag2.py (clean SP/loco/qs/clean2/animator_qs2.cpp, on 5464ab3a, animator only): no foot held
  -> dzRoot = in.rootMove.z (root's own vertical move) when valid, else the old extrapolation; quick-step hold 0.85;
  snap to the landing probe at the step end. The footHold-held change DROPPED (redundant with rootMove.z: same numbers).
  8 seeds: qs drag 30 Hz blocked 1037 -> 87 cm, blocked+SV 884 -> 140; 60 Hz 1248 -> 78, 578 -> 120; sunk3 slightly
  lower (bl.dn 1.71 -> 1.64), floats max 170 -> 107 (30 Hz blocked up). Full suite: stairs better; hill10 run/jog small
  mixed changes (run down float frames 23 -> 30). Harness clean2 == env-gated (DZMOVE 1).
- st8 = st6 + qsdrag2 + walker-180 trace (orch23.sh: build, then locostairs after orch22's locotour) -> log_st8.txt.
- (a) revised blob c6274fbf4eff2be1f0ca96f728e1e2073068f5f4 (on 5464ab3a, +8/-3; written -w), patch
  SP/loco/qs/qsdrag2_5464ab3a.patch; anim_test ALL PASSED (SP/loco/sl/at_qs2.log, attree5). Waiting: locotour on st7t
  (orch22, for (b)), then st8 locostairs (orch23) for the in-game check of (a).
- 04:4x main's ORDER: (1) finish (a): st8 (walker 180 traced) + locostairs, then sign off c6274fbf (or successor) incl.
  the hill run/jog numbers (flight updates now use rootMove.z). (2) then the STAIR-AWARE STANDING STANCE, bounded: each
  foot stays on its tread, standing and quick steps fitted to treads, stair state kept while standing; aim: stairs17.stops
  near walking levels without worse slides (do NOT sign off the partial l6m fix: descent slide p90 3.9 -> 8 cm).
  (3) after that: the FAST-TURN HOVER (planted foot 4-16 cm up at 6 rad/s walking turns: harness turnwalk 90/135/180 at
  1.4 m/s; the planted foot out of reach while the other is mid-swing; quick step needs the other foot down).

## 17. 2026-10-13 ~05:40: (a) ACCEPTED (snapshot 48) and APPLIED; (2) stair stance candidate clean4, in-game runs queued
- st8 (in game, log_st8.txt): up slide mean 1.73 -> 1.62, p90 3.58 -> 3.33, max 30.0 -> 8.27 (walker 180's slide gone, no
  bigslide); sunk3 up 1.1 -> 1.0%; down identical; trips 27; gland medians unchanged. (a) signed off; main accepted it for
  snapshot 48 and I APPLIED it: shared animator.cpp 5464ab3a -> c6274fbf (guarded git apply of qsdrag2_5464ab3a.patch).
  HEAD was ab5995c (snapshot 47 = clasp; animator 5464ab3a, peds ae083442, character.h 6910e2c3).
- (2) clean candidate = patches/stance.py applied to clean2 (c6274fbf) + HEAD character.h (6910e2c3):
  SP/loco/qs/clean4/animator_qs4.cpp + character_qs4.h (additive fields: scanForStep, stepScanAsked/Have, stepG[2][12],
  stepScanFrom/Dir, stepFitShift/Land/Ball). Content: step scan once early in a step on stairs (stairSeen < 1.5), stored
  scan kept with the world (dzRoot + root motion), fitTread/fitStep (swing fit rules) on the step's landing, step ground =
  fitted tread (hold rule kept), step end on the fitted tread (+ ballProbe if the heel overhangs, plantSlack = shift), else
  the snap; stairSeen only advances while walking; standing step skipped if its fitted landing is where the foot stands
  (< 4 cm, yaw < 0.3); a foot that comes down while standing on stairs (other foot planted) does so as a 0.22 s, 2 cm lift
  step from where it is shown; resets in both reset sites; PLUS the clamp fix: a held foot whose probe is at the game's
  0.45 clamp (|gIn| or |groundRaw| > 0.44) is not used for dzRoot and is kept with the world (no snap to the clamped value)
  - this fixed a 16 cm float (stored fit heights drifted 6 cm when the held probe clamped; blocked 60 Hz).
  Harness: clean4 == gated knobs (LM_STEPFIT LM_SEENWALK LM_STANDSKIP LM_STANDPLANTSTEP LM_DZCLAMP=2 on a2) bit-exact on
  stops (8 runs) and crowd/blocked/SV (48). Stops (4 seeds) a2 -> c4: 30 Hz sunk3 up 33.5 -> 4.2, dn 20.5 -> 2.1; slide p90
  up 13.3 -> 2.5, dn 5.6 -> 1.6; max up 76.6 -> 25.1; down float frames 309 -> 195; scuffs up 18 -> 49 (brief touches that
  replace long sinks: stepping-foot frames under -10 cm 1249 -> 206 at 30 Hz, 2483 -> 347 at 60 Hz). 60 Hz: sunk3 up 32.9
  -> 3.9, dn 19.5 -> 2.1, slide p90 up 11.0 -> 1.5, up float frames 149 -> 172. Walking stairs (walk/crowd/blocked 8 seeds):
  within noise; blocked 30 Hz worst float frame 107 -> 129 mm (single-frame reach float: the fit shifted a 50 cm backward
  quick step 13 cm further back; pc 0.57 m) - mention in the sign-off. anim_test ALL PASSED (sl/at_qs4.log, attree6).
  Full suite: only stairs17.* change.
- In game: orch24.sh (05:34): builds st9 (= st8 tree + stance.py + apply_stops.py: LOCO_STOPS=1 env makes the
  locostairs walkers stop 2 s after every 2.5 s on a flight; standing walkers counted by heading) and st8s (st8 + the
  option); runs (one at a time, not before 05:45, not while SP/locotour/hold exists) st9 locostairs -> log_st9.txt, st9
  stops -> log_st9s.txt, st8s stops -> log_st8s.txt. run_st2.sh <tree> <tag> [stops].
- Then: sign off (2) = clean4 (write blobs -w: animator on c6274fbf, character.h on 6910e2c3), with in-game numbers.
  Then (3) the fast-turn hover.
- 05:45-06:00: main accepted (a) for snapshot 48; APPLIED (shared animator 5464ab3a -> c6274fbf). Hold lifted 05:45;
  orch24: st9 built 05:42, st8s 05:59; st9 locostairs running from 05:45 (then st9s stops, then st8s stops).
- (2) scuffs: stops-up scuffs 18 -> 49 = brief touches replacing long sinks (stepping-foot frames under -10 cm 1249 ->
  206 at 30 Hz); sources: the stand-plant step starting at the swing's lagging fz (foot 17 cm in the tread 1-2 frames),
  stale ballProbe after a walk-restart re-plant (heel -3.7 cm). Left as is (explain in the sign-off).
- (3) FAST-TURN HOVER, harness (knobs in SP/loco/qs/tree: LM_TURNQSU, LM_TURNSLIDE, LM_TURNREL, LM_TURNHURRY,
  LM_TURNOVERLAP, LM_TURNEMERG; trun.sh <tag> <env> + tstat.py <hz> tags): the big floats (10-16 cm) are a planted foot
  out of reach (pc 0.43-0.72 m) while the OTHER foot is in a quick step (hurry branch) or a gait swing (no branch).
  Slide clamp kills floats but gives 37-50 cm slides (rejected). Best simple rule = LM_TURNEMERG=0.40: past |pc| 0.4*scale
  it quick-steps even with the other foot off the ground. 60 Hz turnwalk 90/135/180 at 1.4: float frames 35/71/62 ->
  22/14/31, float max 106/155/158 -> 75/69/48 mm; 30 Hz 26/37/37 -> 13/11/16, max 109/139/151 -> 70/57/40; slides ~same
  (180 at 60 Hz p90 2.34 -> 2.48). Full suite: only turnwalk, stairs17 blocked/stops, stop.jog (floats 3 -> 1), pass.headon
  (60 Hz floats 10 -> 8), hill run up (tiny); blocked down worst float frame 165 -> 188 mm (30 Hz). Clean patch:
  patches/turn.py -> clean5/animator_qs5.cpp (= clean4 + turn), harness build lm_qsc5 (bld5.log) to verify == e40o.
- 06:00 main: WIND-DOWN. Finish (2) fully (st9 / stops runs, then the up-scuff and float-frame investigation); sign off if
  worth shipping, else park cleanly. Start nothing new: (3) PARKED (its harness build lm_qsc5 stopped by PID before it
  ran; clean5/animator_qs5.cpp + patches/turn.py stay for later). Then update this file, make sure no games/chains run,
  send main one short "parked" message.
- (2) float frames (60 Hz stops up 149 -> 172): +78 are kind-3 (held re-plant) feet planted 0.4-0.6 s with pc 0.41 m,
  floating 2-8 cm: a fitted step's plantSlack (|shift| up to 0.2 m) lifts the walking reach limit to 0.5 m, beyond the
  leg's reach with the foot two treads below the root. Knob LM_SLACKCAP=x (walking: limR = min(lim + slack, x) x scale)
  in the gated tree; capt.sh <tag> <env> runs stops + crowd/blocked for a variant.
- 06:24 st9 (in game, walking locostairs, candidate clean4) vs st8: identical within noise - up slide 1.62 / 3.33 / 8.27
  (same), sunk3 1.0 -> 1.1%, sunk1 1.3 -> 1.4%, scuffs 21 -> 22; down identical; trips 27; no bigslide; deep lines 143 ->
  147; landing errors (gland) same. st9s (stops) started 06:24, then st8s (stops). Harness rebuild for LM_SLACKCAP held
  at the memory gate (4 games running).
- 8-seed stops (harness, clean binaries) c2 = (a) -> c4 = (2): 30 Hz up slide mean 5.86 -> 1.38, p90 15.2 -> 2.7, max 76.6
  -> 25.1, sunk3 32.8 -> 4.05%; down p90 5.1 -> 1.6, sunk3 17.5 -> 2.0%, lifted 5.2 -> 3.4, float frames 321 -> 207;
  up scuffs 19 -> 48, down 51 -> 42. 60 Hz up mean 5.40 -> 1.03, p90 13.5 -> 1.67, max 72.6 -> 38.9, sunk3 32.6 -> 3.75,
  float frames 147 -> 170; down p90 2.8 -> 1.6, sunk3 17.1 -> 2.0, floats 611 -> 420, scuffs 54 -> 30. (Crowd walking on
  the same stair: sunk3 3.6 / 5.4% at 30 Hz, 2.9 / 2.7% at 60 Hz.)
- 06:28 LM_SLACKCAP tested (gated rebuild; without the knob it reproduces fkc2 exactly): cap 0.40 brings 60 Hz stops-up
  float frames 172 -> 149 (= (a)'s 147) but stops-up sunk3 3.91 -> 4.04 (single-seed full suite 4.00 -> 4.28) and a new
  12 cm slide in one blocked run; cap 0.36 worse (sunk3 4.75). A trade, not a win -> NOT in the candidate (parked idea).
- (2) candidate FINAL = clean4: blobs written -w: animator 7471cb4ab5619c5dd5428234a8a5b2edac18fb7f (on c6274fbf,
  +150/-16), character.h b745372ded59cba98aa4210bb107c51c9f6a8313 (on 6910e2c3, +9 additive). Patches (git apply
  --check passes on the shared tree): SP/loco/qs/stance_animator_c6274fbf.patch, stance_character_6910e2c3.patch.
  anim_test ALL PASSED (sl/at_qs4.log). Waiting: in-game st9s (stops, candidate) and st8s (stops, (a)) via orch24.
- 07:15 st9s (in game, STOPS, candidate clean4): up slide 2.01 / 4.11 / 26.7, sunk3 7.4%, scuffs 44; down 1.35 / 3.46 /
  30.7, sunk3 4.2%, lifted 3.9%; trips 16; 7 bigslides; deep lines capped (400). Main in-game failure modes (traces):
  (1) the last swing at an abrupt stop lands 15 cm short of its plan, across a nose (heel in the riser going down), and
  stays there for the stop; (2) a standing step overtaken by the walk resuming heads 30-50 cm on, off its one scan, and
  lands unfitted across a riser (harness: the biggest sunk category, k4 while walking).
- Harness fix for (2): LM_STEPRESCAN=0.75 (scan again while u < 0.75 once the landing left the stretch scanned):
  stops 8 seeds fkc2 -> rs: 30 Hz sunk3 up 4.05 -> 1.92, down 2.01 -> 0.53, slide p90 up 2.70 -> 2.10; 60 Hz sunk3 up
  3.75 -> 1.00, down 2.04 -> 0.39, p90 up 1.67 -> 1.24 (up floats 170 -> 175). Crowd/blocked: better or same (30b up sunk3
  2.25 -> 1.63, worst float 129 -> 107 = (a)'s; 30s up one slide 10.5 -> 20.1). Tried and rejected: LM_SWINGKEEP (store
  swing scans) + LM_STANDFIX (re-place a standing foot found across a riser): sunk a little lower but down slide p90
  1.56 -> 3.75-4.22.
- clean6 = clean4 + patches/rescan.py (SP/loco/qs/clean6/animator_qs6.cpp; character.h unchanged b745372d). Running:
  qs/v6.sh (harness build lm_qsc6, compare with gated rs, full suite, anim_test at7.sh) -> qs/v6.log; orch25.sh (st10 =
  st9 + rescan: build, then after orch24 st10 stops -> log_st10s.txt, st10 walking -> log_st10.txt) -> locotour/orch25.log.
- 07:54 st8s (in game, STOPS, (a) = baseline) vs st9s (clean4): up slide mean 7.92 -> 2.01, p90 31.9 -> 4.1, max 76.7 ->
  26.7, sunk3 23.9 -> 7.4%, lowest sole -262 -> -169 mm, scuffs 25 -> 44; down mean 1.90 -> 1.35, p90 6.2 -> 3.5, max
  36.7 -> 30.7, sunk3 17.2 -> 4.2%, lifted 5.0 -> 3.9%; slides over 15 cm 38 -> 7; trips 16 both. (Walking: st9 = st8.)
- 08:00 clean6 VERIFIED in the harness: identical to gated rs on all 64 stair runs (stops 8 seeds x 2 rates, crowd /
  blocked / blocked+SV 8 seeds x 2 rates); full suite (a) -> clean6 changes only stairs17.*. Blob animator
  0818e30c910af21faca26e1ecac08069c97a0ec4 (on c6274fbf, +159/-16, written -w) + character.h b745372d (on 6910e2c3);
  patches SP/loco/qs/stance6_animator_c6274fbf.patch + stance_character_6910e2c3.patch (git apply --check OK on the
  shared tree). anim_test (at7.sh -> sl/at_qs6.log) and in-game st10s / st10 (orch25) running.
  Harness (a) -> clean6, stops 8 seeds: 30 Hz up slide mean 5.86 -> 1.08, p90 15.2 -> 2.1, max 76.6 -> 25.1, sunk3 32.8 ->
  1.9%; down p90 5.1 -> 1.6, sunk3 17.5 -> 0.5%, floats 321 -> 195; up scuffs 19 -> 47. 60 Hz up mean 5.40 -> 0.68, p90
  13.5 -> 1.24, sunk3 32.6 -> 1.0%, up floats 147 -> 175; down sunk3 17.1 -> 0.4%, floats 611 -> 408.

## 18. 2026-10-13 ~09:20: PARKED (wind-down). (2) SIGNED OFF; (3) parked; nothing running
### Where everything stands
- Shared tree: src/anim/animator.cpp = c6274fbf (the (a) quick-step drag fix, accepted for snapshot 48, applied by me with
  the 5464ab3a guard); character.h = 6910e2c3 (HEAD); peds.cpp = HEAD (ae083442). All uncommitted changes there are
  others' except animator.cpp.
- (2) stair-aware standing stance SIGNED OFF to main (09:20), NOT applied (waiting for main's word):
  animator 0818e30c910af21faca26e1ecac08069c97a0ec4 on c6274fbf (+159/-16) = SP/loco/qs/clean6/animator_qs6.cpp
  (= patches/stance.py + patches/rescan.py on clean2); character.h b745372ded59cba98aa4210bb107c51c9f6a8313 on 6910e2c3
  (+9 additive) = clean4/character_qs4.h. Patches with guards: SP/loco/qs/stance6_animator_c6274fbf.patch,
  stance_character_6910e2c3.patch. If main says apply: check animator == c6274fbf and character.h == 6910e2c3, then
  git apply both (shared tree), confirm blobs 0818e30c / b745372d.
- In game (locostairs): stops st8s ((a)) -> st10s ((2)): up sunk3 23.9 -> 3.9%, slide mean 7.92 -> 1.78, p90 31.9 -> 3.6,
  max 76.7 -> 26.7; down sunk3 17.2 -> 2.6%, p90 6.2 -> 3.8; slides > 15 cm 38 -> 8; up scuffs 25 -> 44. Walking st8 ->
  st10 identical (sunk3 up 1.0 -> 1.1%); landing errors same. anim_test ALL PASSED (sl/at_qs6.log).
- Known leftovers of (2): up scuffs (brief touches replacing long sinks); 60 Hz stops-up float frames 147 -> 175 (slack of
  fitted feet at walk restarts; LM_SLACKCAP=0.40 fixes it but adds sunk frames - a trade); the abrupt stop's last swing
  can land ~15 cm short of its plan across a nose and stay there (LM_SWINGKEEP + LM_STANDFIX tried: worse down slides).
- (3) fast-turn hover PARKED: harness knob LM_TURNEMERG=0.40 (quick step past 0.4 m reach even with the other foot off
  the ground) is the best simple rule (turnwalk float frames -35..-80%, max 106-158 -> 48-75 mm; full suite changes only
  turnwalk, stairs17 blocked/stops, stop.jog, pass.headon slightly). Clean patch patches/turn.py (on clean4 ->
  clean5/animator_qs5.cpp; must be re-applied on clean6 if resumed), never built clean or tested in game. Also seen:
  remaining turn floats are the first frame after a quick step lands (pin ease-in) and toe-off at stance end.
- Debug tree SP/loco/qs/tree = everything above behind LM_* env knobs (lm_qs binary); clean binaries sl/lm_qsc2 ((a)),
  sl/lm_qsc6 ((2)). Scripts: qs/srun2.sh/srun3.sh (stops 4+4 seeds), qrun2.sh (crowd/blocked 8 seeds), capt.sh/capt8.sh
  (variant tests on the gated binary), sstat8.py / mstat.py / qstat.py / tstat.py; locotour/run_st2.sh <tree> <tag>
  [stops] (LOCO_STOPS test option in st8s/st9/st10 trees via locotour/apply_stops.py), lsum.sh <logs>.
- Nothing of mine is running: orch24/orch25 done, game exes deleted, harness builds finished, monitors ended.
### Next (when resumed, if main wants): apply (2) on main's word; then (3) the fast-turn hover from the notes above.

## 19. 2026-10-13 ~09:25: (2) ACCEPTED for snapshot 49 and APPLIED; parked
- Main: (2) accepted for snapshot 49 (snapshot 48 = (a) c6274fbf, in its checks). Applied with guards (animator.cpp was
  c6274fbf, character.h was 6910e2c3): shared tree now animator.cpp 0818e30c910af21faca26e1ecac08069c97a0ec4,
  character.h b745372ded59cba98aa4210bb107c51c9f6a8313 (uncommitted; face.cpp there is the faces agent's).
- NEXT when resumed (main's order):
  1. The FAST-TURN HOVER (section 18: LM_TURNEMERG=0.40 in the gated tree; patches/turn.py - re-apply on clean6 =
     the shared animator 0818e30c, build clean, harness full suite, anim_test, in-game locotour turning check).
  2. The ABRUPT-STOP SHORT LANDING: at a hard stop on stairs the last gait swing lands ~15 cm short of its plan, across a
     nose (in game: heel in the riser going down, toe in the riser going up), and the foot stays there for the stop.
     Leads: the landing's residual from its plan at the gait plant (stairShift - plantCorr along md) flags it; a fitted
     settle step for that foot once standing (the swing scan could be kept as LM_SWINGKEEP does, but SWINGKEEP+STANDFIX
     as tried worsened down slides p90 1.56 -> 3.75-4.22 - restrict it to the flagged foot only).
- Nothing of mine is running.

## LEAD NOTE (10-13 11:20 UTC): PR #47 (snapshot 49, 7166a69) is OPEN because of a Codex P2 finding
- Codex comment 4257933215 on src/anim/animator.cpp:773: when stairSeen crosses 1.5 s mid-step (u < 0.75), stepTo reverts
  to the unfitted heel while stepFitLand/stepFitShift/stepFitBall keep the last fit, so touchdown takes the `fitted` branch
  with an unfitted landing. Confirmed (lead replied on the thread).
- Fix first thing on resume: when the fit is abandoned mid-step, clear the cached fit (stepFitLand = -9, stepFitShift = 0,
  stepFitBall = false), so touchdown falls back to the ground probed where it lands. Then run the stairs harness + an
  in-game locostairs run, sign off a blob on 0818e30c, the lead snapshots it onto PR #47, replies, resolves and merges.

## 20. 2026-10-19 resumed (after the reboot): PR #47 Codex P2 fix, then the fast-turn hover
- HEAD claude/neon-tide 0d67933 (snapshots 49-51 on PR #47, open); main af8a066 (v0.02.6). HEAD animator 0818e30c = (2),
  character.h b745372d. Shared tree = HEAD for my files.
- P2 FIX = patches/p2fix.py on 0818e30c -> SP/loco/qs/clean7/animator_qs7.cpp = blob d4aaddc88c252b851a5ac96b7aeab65c7c74e83b
  (written -w); patch SP/loco/qs/p2fix_animator_0818e30c.patch (git apply --check OK). dropFit(s) clears stepFitLand /
  Shift / Ball; fitStep starts with it; the u < 0.75 branch calls it when stairSeen >= 1.5 (the abandoned fit).
  Harness: the P2 case happens (instrumented c6: 245 update-frames over the stair runs, e.g. stepT 0.28 seen 1.53 with a
  stale fit 0.17 / shift 0.023); clean7 vs clean6: all 66 result files identical except 4 blocked runs with one extra
  ground scan (no metric changes). anim_test ALL PASSED (sl/at_qs7.log). In game: orch26 (st11 = st10 + p2fix; locostairs
  -> log_st11.txt).
- TURN HOVER on the fixed file: clean8 = clean7 + patches/turn.py (lm_qsc8): turnwalk floats 60 Hz 35/71/62 -> 22/14/31,
  max 106/155/158 -> 75/69/48 mm; 30 Hz 26/37/37 -> 13/11/16, max 109/139/151 -> 70/57/40; 180 deg slide mean 1.23 -> 1.40
  (60 Hz); outside turns: blocked stairs a little better (floats, sunk), stop.jog floats 3 -> 1; scuffs +3..5 in blocked.
  Stairs 8 seeds neutral. anim_test ALL PASSED (sl/at_qs8.log). In game: orch27 (st12 = st11 + turn; locotour on st11's
  copied exe -> log_st11t.txt as baseline, then st12 -> log_st12t.txt).
- 14:30 main: P2 fix (d4aaddc8) received, snapshot 52 building; main grafted it into the shared tree (animator =
  d4aaddc8). Sign the turn hover off against d4aaddc8 (clean8 = 4ea1dde200dee12367d0926df638c683b6779a78).
- ABRUPT-STOP SHORT LANDING (item 3), harness, gated knobs (tree; lm_qs): what is left of stops sunk after (2)+rescan is
  mostly 'stand k1/k3' (the last gait landing at the stop, across a nose) = ~50-65% of the sunk frames. Tried:
  LM_MISSFIX=x (flag a gait landing > x off its fitted plan; standing on stairs, a flagged foot takes a fitted re-place
  step) and LM_MISSFIX2=x (flag by the landing's violation of its swing's scan, kept apart in swingG; LM_MISSFIX=99 to
  use it alone), LM_FIXLIFT (normal standing lift for the re-place step). All the same outcome: stops sunk3 30 Hz up 1.92
  -> 1.46, down 0.53 -> 0.39; 60 Hz up 1.00 -> 0.84, down 0.39 -> 0.35; but 30 Hz down slide p90 1.56 -> 2.66-2.78 (the
  foot slides out of the riser it stands in as the step starts). Not a clear win -> PARKED (ideas: lift the foot clear
  first - step up and over the nose - or re-fit the landing during the swing's last frames as the walk brakes).
- 15:42 main: memory priority for the night agent: after the current locotour run, no new game or compile until its gate
  (PID 18775, sh /tmp/night/gate.sh 2500) passes or 16:10. SP/locotour/hold created (orch27's runs wait on it); a waiter
  removes it then. st11t (locotour baseline, HEAD + P2) done: turning n 200, slide mean 4.65, p90 8.80, max 127.9;
  planted-floating frames 154; st12t (turn hover) running since 15:30.
- TURN HOVER sign-off material: blob 4ea1dde200dee12367d0926df638c683b6779a78 (clean8, written -w) on d4aaddc8,
  +9/-4; patch SP/loco/qs/turn_animator_d4aaddc8.patch (git apply --check OK on the shared tree).
- 16:15 st12t (locotour, turn hover) vs st11t: turning slide mean 4.65 -> 4.50, p90 8.80 -> 8.32; straight/standing same;
  planted-floating frames 154 -> 159; big slides 19 = 19 (the tour's turns are 1-3 rad/s: the rule rarely fires).
  TURN HOVER SIGNED OFF to main: blob 4ea1dde2 on d4aaddc8 (patch qs/turn_animator_d4aaddc8.patch), not applied.
- Abrupt-stop short landing PARKED (see above). Nothing of mine running.

## 21. 2026-10-19 ~16:20: turn hover ACCEPTED (snapshot 53, main grafted 4ea1dde2); NEW ITEM = turns on starts / stops
- Shared tree animator = 4ea1dde200dee12367d0926df638c683b6779a78 (P2 fix + turn hover), grafted by main.
- My pick (approved): peds turning while starting / stopping on flat ground = the locotour's biggest slides (8 of 19 over
  20 cm: turning 1.7-3.2 rad/s, moveW 0.2-0.9, speed 0.36-0.98; worst ped 13 128 cm / 148 deg pivot, a group follower).
  Follow-ups if they share a cause: 6 straight-walk slides 20-53 cm at full speed; ped 107's idle foot creeping 35 cm.
- Game facts: movePed accel 11 (faster) / 16 (slower) m/s^2; BRAIN_GOTO faceTowards at 8 rad/s; the AI's wander turnTo
  6 rad/s (main's note: AI agent's item; help it with turn-harness numbers if asked).
- Harness: locometer.cpp got startStopTurns() (backup locometer_pre_tss.cpp): turnstart.90/180, turnstop.90/180,
  corner.90_slow / 150_slow (label 12). Building sl/lm_t8 (ctree8 = shared-tree animator + turn) - memory gated.
- In game: st13 = st12 + apply_trace.py (LOCO_TRACEPED=uid,... per-frame trace of peds in the locotour); orch28.sh
  builds it and runs the locotour tracing 13, 35, 12, 114 -> log_st13t.txt.
- 17:19 main: memory priority - after loco_st13 ends, no new game / compile until the AI agent's nightwalk run is up and
  the faces gate PID 17353 is gone (fallback 18:00 UTC): SP/loco/prio_ok.sh checks it; SP/locotour/hold set, a waiter
  lifts it. AI facts: HEAD pedai caps walkers at 3 rad/s (0.35-2.4 m/s asked) and cuts the pace for turns over 60 deg (to
  0.3); its change 3 (AW/change3.py, nt_ai102, after set 17): ai_turn rate(v) = lerp(4, 2.4, (v-0.1)/0.3), the move
  eased to the body (within 0.6 rad of the facing from 0.5 m/s), pace down to 0.08 by 130 deg; also GOTO/FOLLOW/police.
  Harness: locosim.h control() got LM_TURNPROF=1 (HEAD pedai) / 2 (change 3) (backup locosim_pre_prof.h); build lm_t8p
  queued behind prio_ok. The AI agent will ask for turnwalk 90/135/180 numbers on change 3 after set 17.
- 17:30 traced locotour (log_st13t.txt; LOCO_TRACEPED=13,35,12,114; same results as st12t): three different causes:
  (1) ped 13 (group follower, leader 7): 0-2 m/s (0.47 mean), walk weight 0.42, the turn rate flipping sign every 5
  frames (+-3/6 rad/s): feet released (toe-off / reach) at low walk weight slide along the ground to the pose's foot
  before the next plant (the 128 cm = several such moves in one 74-frame contact); (2) ped 35: a 70-deg turn at 3 rad/s
  at 1.2-1.4 m/s CRABBING (side velocity 0.7-0.9): the swing foot skims the ground (toe within 8 mm) for 4 frames (75 cm);
  (3) ped 12 (straight, 1.28 m/s): the swing touches down 3.5 cm under the ground before the swing ends (ground higher
  than the animator's - a kerb?) and runs on 53 cm. AI change 3 (arcs, no crab) should remove (2); (1) is the target.
- Harness: locometer.cpp got shuffle.follow / shuffle.follow_fast (label 13; backup locometer_pre_shuffle.cpp): the
  pace wavering about 0.45 / 0.75 m/s, the way swinging +-20-35 deg. Needs a rebuild (memory: 4 games, memfree 82 MB).

## 22. 2026-10-19 18:15 - ROOT CAUSE of the tour's big slides: stance-change crossfade after the foot IK
- Harness: locosim.h control() kept a stale turnRate when not turning (desired < 0.2 m/s): planted feet spun. Fixed (old
  behaviour: LM_STALETURN=1; backup locosim_pre_tr0.h). locometer.cpp: LM_TRACE line has pz/pe/hd + sk (legSink);
  replay() scenario: LM_REPLAY=<locotour log> LM_REPLAY_UID=n [LM_REPLAY_T0/T1, LM_REPLAY_WHO], run with LM_HZ=30 -
  plays a traced ped's recorded motion (yaw, turnRate, in.speed, md, stance, pos) to the 6 people. Backup
  locometer_pre_reach.cpp. htrace.py parses LM_TRACE (with dr/pr from the debug tree dtree8 = IK miss / target heel).
- The replay reproduced ped 13's slides (max 150 cm); IK reached (dr 0, pr<2cm) but the final pose was off: the
  stance-change snapshot crossfade (snapW, blendCtl after footPlanting). pedai flips a follower's stance 0<->7 every few
  frames (ped 13: 27 flips; "chatting on the way" needs length(desired)>0.2, "leader stopped" lspd<0.3 && d<1.2);
  ped 12 8->0, ped 35 10->0, ped 114 7<->0: EVERY traced big slide sits on a stance change.
- Fix (patches/snapstance.py, on animator 4ea1dde2 + character.h b745372d): F1 stance changes among {0,7,8,10,23}
  crossfade the upper body only (new Animator::snapUpper, reset at the other snap captures); F2 a held foot re-planted
  by the gait or coming to a stand keeps its footprint (was the shown heel = new pose + old corr: one update's jump).
  clean9/animator_qs9.cpp = b251d0ea, clean9/character_qs9.h = 1628ef2e. ctree9 / sl/lm_t9; attree10 anim_test ALL PASSED.
- Replays (30 Hz, 6 people) base -> clean9: ped 13 mean 7.77->0.79 p90 21->1.5 max 150->4.7; ped 12 2.69->0.69,
  max 52->11.6; ped 35 2.17->1.22, max 39->11.6; ped 114 4.52->0.86, max 44->5.1. Harness suite (r9/*): F1 no effect
  (no stance changes); F2 124/139 better vs 10/18 worse (30/60 Hz): straight walks (walk_slow_0.9 0.54->0.21 cm/step),
  stairs down (float max 159->16 mm). Worse: a few float frames in turns (sink-capped reach, pre-existing), scuffs,
  stairs17.blocked.up one step 5.6->9.7. Profiles 1/2 the same picture.
- In game: orch29.sh (detached 18:13): st14 = st13 + snapstance -> log_st14t.txt (trace 13,35,12,114,107) then
  log_st14s.txt (locostairs). Compare with log_st13t.txt / log_st11.txt.
- 18:36 main approved: run st14 locotour + locostairs, then sign off (include the worse stair step 5.6->9.7 with its
  cause: stairs17.blocked.up, a held foot with its toes into the next riser; the old re-plant jump happened to pull the
  ball back over its tread; kept, the toes stay in the riser until the quick step drags the toe tip out = 9.7 cm).
  Blobs written (git hash-object -w): animator b251d0ea, character.h 1628ef2e; patches qs/snapstance_animator_4ea1dde2.patch,
  qs/snapstance_character_b745372d.patch (git apply --check OK on the shared tree); graft text qs/snapstance_graft.txt.
  NEXT ITEM (approved): the ball-pivot heel lift. Cause: the IK's pivot (heel up 0.2 rad about the ball) runs after the
  pin, which put the pose's lowest point (the heel, on a toe-up foot) on the ground -> the whole foot floats 2-4 cm.
  Tried F3a (wBall = max(wBall, pivotW)): mixed (floats 35->22 in turnwalk.180 but corner.150 max 35->54 mm).
  F3b in dtree8 (LM_PIV2OFF=1 disables): roll the pose's foot about its ball at the top of footPlanting (fp, fq, toes
  flat), so contact, pin and IK see the pivoted foot; the IK block's rotation skipped. Results r9/d3b_*.txt (job bw41xjupu).
  dtree8 also has F1/F2 switches (LM_SNAPUP0, LM_HOLDKEEP0) and dbgDrift = toe tip - ball, dbgPred = pose pivot z.
- Creeping idle foot (ped 107, ACT_HAIL_TAXI): waves every 3-6 s as a FULL-body action while standing (actionUpper needs
  speedS > 0.7) - planting off during the clip; to check in the st14t trace (107 traced).
- anim_test baseline on HEAD files (attree10b, at_qs9b.log) vs patched (attree10, at_qs9.log): ALL PASSED both. Gait skate
  maxima mostly down (seed 140 v0.9 0.827->0.032 m/s, seed 77 v7.0 0.902->0.143); seed 12 v1.7 p95 0.006->0.057, limp
  planted-only mean 0.0014->0.0055 (limit 0.02): anim_test's SoleTrack counts planted&&wasPlanted frames only, so the
  old re-plant jump (held->planted frame) was never counted. locometer agait.* (same seeds/speeds as anim_test, all
  contact frames, rootMoveOff, + limp_1.0 with legHurt[1]) improved at every speed both rates (1.7: 1.23->1.04 cm/step,
  limp 1.38->1.04 at 60 Hz). r9/ag_*.txt.
- st14t game started 18:50 (queued for a wine slot 18:22-18:50); monitor bh0deexgb.
- 19:30 st14t (log_st14t.txt) vs st13t: all steps mean 2.40 -> 0.82 cm, p90 4.42 -> 1.59, max 127.9 -> 37.4; straight
  1.58 -> 0.73 (max 53 -> 11.6); turning 4.50 -> 0.84 (max 128 -> 27.6); standing 3.49 -> 1.73 (p90 12.4 -> 1.8, max
  37.4 = ped 107); slides > 20 cm 19 -> 3 (ped 107 x2, ped 15 stance 11); planted floats 159 -> 114; scuffs 41 -> 50.
  Ped 107 (taxi hailer, never moves): each CLIP_WAVE (full-body while standing, planting off) shifts foot 0 ~6 cm at its
  start (the correction fades with plantOn) and ~6 cm back at its end (crossfade + plantOn ramp) -> 36 cm "creep" over
  20 s. Fix idea: keep planting during gestures (wave/point) or play them upper-only when standing. Follow-up item.
  st14s (locostairs) running since 19:30:33.
- F3c in dtree8 (LM_PIV=3 default; 0 = c9 exactly, 2 = F3b): roll to the contact (none on a heel strike, else up to
  0.2 rad heel-up incl. the pose's own pitch), twist about the contact (heel / ball / toe tip). vs c9: ~70 better vs ~25
  worse per profile/rate; floats cut (turnstart.180 34 -> 4, corner.90 74 -> 32, shuffle 21 -> 2, turnwalk.180 35 -> 10),
  slides about level (turnstart.90 0.96 -> 1.08 worse, turnstop.180 0.86 -> 0.62 better). r9/d3c_*.txt.
- 19:5x MAIN (priorities): the user is replacing the procedural world with a hand-built one, every building enterable
  (every floor and room): stairs, landings, doors, tight interiors everywhere. Order: (1) stance-crossfade sign-off,
  (2) ball-pivot heel lift, then (3) PRIORITY: interior walking robustness - stairs of every kind (straight, switchback,
  spiral if feasible), landings, doorways, turning in tight rooms - ahead of minor flat-ground polish (ped 107 wave creep,
  ped 15 locked stance, reach-capped floats = lower priority unless they hit interiors).
- 20:20 locostairs st14s vs st11: up unchanged; down mean 0.84->0.75, p90 3.16->2.99; deep 146->161 = one new episode
  (ped 180 dn foot 0, 15 frames: a late-swing graze hold short of its fitted tread, kept when planted, heel 17 cm in the
  riser behind; old re-plant jump carried it past the nose). SIGN-OFF SENT to main 20:2x (blobs b251d0ea / 1628ef2e).
  Next: F3 (ball pivot) polish + sign-off; then interiors. Stairs follow-up: fit a grazed hold onto its tread.
- 20:3x MAIN: stance-crossfade fix ACCEPTED = snapshot 55 (after 54 = AI set 17). Do NOT graft; main puts the blobs in
  the shared tree when 55 commits. Bases hold. Grazed-hold stairs episode + blocked-stairs step go with interior stairs.
  F3 clean patch: patches/pivot.py on clean9 (b251d0ea) -> clean10/animator_qs10.cpp = ab75c2e4. Tree ctree10.
- 20:50 F3 variants: F3d (clean10 first) ~70 better/25 worse vs c9; F3e (old 0.2 roll kept on heel-up feet) worse;
  F3f = F3d with the toes-up roll scaled by pw (continuity) -> patches/pivot.py now = F3f; clean10/animator_qs10.cpp
  = 002915d0; job b6fp5e6oo (c10f_*). st15 tree = st14 + pivot.py (in-game test next).
- INTERIOR harness (locometer.cpp, backups *_pre_interior.*): Terrain kinds 5 switchback, 6 spiral (layered via
  mutable T.zRef set at control/animate/footWorld), 7 L stair; runOne(start pos); PathFollow (pure pursuit 0.6 m);
  interior(): switchback.up/down_1.2, lstair.up/down_1.2, spiral.up/down_0.8, room.doorway_1.1, room.turnback_1.1;
  label 20; stair stats sunk/sunk3/deep/lifted/quick. NOT YET BUILT (memory).
- 21:07 orch30.sh (detached): build st15 (= st14 + pivot.py, animator 9d805732), locotour (trace) -> log_st15t.txt,
  locostairs -> log_st15s.txt. qs/job_i1.sh (detached): anim_test attree11 (clean10 = 002915d0) -> sl/at_qs10.log; then
  lm_t9i (ctree9 + interior harness) and lm_t10 interior runs (switchback/lstair/spiral/room, prof 0/1/2, 30/60 Hz)
  -> r9/int_c9_* / r9/int_c10_*. F3f vs c9: 66-74 better vs 23-28 worse (= F3d).
- 21:15 anim_test with pivot patch: ALL PASSED, only timing lines differ. INTERIOR harness first results (r9/int_*):
  BIG pre-existing problems (c9 and c10 alike): spiral.up slide mean 9-14 cm/step, max 82-86 cm, sunk 30-38%; spiral.down
  lifted ~20%, floats 230-418 frames max 19 cm; lstair.up prof2 max 65-83 cm; switchback/lstair DOWN floats 60-144 max
  157 mm (feet hanging 15 cm); rooms fine. Spiral geometry note: walking line R=0.7 gives 24 cm treads under the inner
  foot - widen (rOut 1.3, R ~0.9) to be realistic. Scans along the tangent leave a curved stair (read the floor below).
- 21:30 harness metric change: soleClear() (locosim.h) = least clearance along heel->ball->toe (9 pts per part) for
  floats/lowest (Tracker) and stair sunk/lifted/deep (LM_SOLE3=1: old 3-point). Stairs-down "floats" of 15.6 cm were
  feet rolling over a nose (not real); with it: stairs17.walk.down floats 23-48 frames max 3.5-5.6 cm; straight stairs UP
  now show sunk 1 cm 5-7% (3 cm 0.4-1.3%). r9/int2_* (c9 = lm_t9i, c10 = lm_t10).
- INTERIOR diagnosis so far: spiral.up: swing forefoot passes THROUGH the next tread (b -6 cm while moving 11 cm/frame):
  the landing probe/scan go along the tangent md - on a curve they sample lower treads (and leave the annulus, reading
  the floor below). Needs landing prediction along the turning path (turnRate x time to landing) for probes + scans.
  lstair.up prof2: a quick (turn) step onto flight 2 lands with the forefoot inside the first riser (-13 cm), then
  planted heel-high: step fit fails on the turn. Spiral geometry: test also a comfortable one (rOut 1.4, R 0.95, 18 deg).
  stairs17.crowd.down: real floats (146-357 frames, max 17 cm).
- 21:42 MAIN memory priority: autoupdater gate PID 14554 (sh /tmp/upd/gate.sh 2500) first: after my current game/compile,
  no new one until PID 14554 is gone or 22:30 UTC. st15t locotour (running since ~21:38) may finish; hold file set so
  orch30's locostairs waits; unhold.sh (detached) removes it when prio_ok.sh passes. Stopped the queued lm_d10 build.
  dtree10 = ctree10 + curved landing prediction (turnOver/landAhead, stairDir member, LM_CURVE0=1 disables) - unbuilt.
- 21:5x MAIN (stairs in the hand-built world, US/Florida code): (1) switchback U stairs everywhere (2 flights/storey,
  180 landing, riser 0.17-0.18, tread ~0.28); (2) L + straight in houses (90 landings, some WINDERS = wedge treads at the
  turn); (3) spirals rare: 1.5-2.0 m dia, walk line 0.45-0.6 m from centre, 22.5-30 deg/tread, riser 0.19-0.22;
  (4) grand curved stairs radius 2-4 m; (5) escalators, porch/entrance steps. TUNE ORDER: turn onto a flight from a
  landing (L/switchback re-scan) -> switchback-up sinking -> curves at walk line ~0.5 m and 2-4 m -> tight (<0.4 m) last.
  ALSO WANTED: half-page building-kit spec of stair geometry the feet handle well (riser/tread ranges, min landing depth,
  winder + spiral limits, min door width for turning) -> back it with a harness sweep.
- 22:20 st15t (pivot, locotour) vs st14t: slides level (all mean 0.82->0.84, p90 1.59 same, turning p90 2.03->1.94,
  max 27.6->26.1), scuffs 50->42, planted floats 114->110; big slides the same 3 (ped 107 wave creep x2, ped 15 st 11).
  st15s (locostairs) running since 22:19 (orch30) -> log_st15s.txt; compare: lsum.sh log_st14s.txt log_st15s.txt.
- 22:21 job_i4 (game-like ground query in the layered terrain: window 0.55 above base.z+0.3 probes / +0.5 scans):
  r9/int4_{c9,c10,d10}_p{0,2}_{30,60}.txt; table: python3 loco/intab.py int4 c9,c10,d10 p0_30,p2_60 (run in r9).
  curved R3 is FINE now (was the harness window). d10 (curved landing + rescans) wins on L/switchback/winder:
  winder.up sunk3 13.8->2.5 %, lstair.up 9.3->3.1, switchback.up 4.4->2.6 / 4.2->0.6, switchback_tight.up max slide
  45->4.8 (p2_60) BUT there sunk3 3.8->6.8 and a 169 mm float. Spirals (R0.5-0.55) still broken: slide max 41-97 cm,
  sunk3 10-33 %. Down stairs: floats 56-176 frames, max 51-106 mm (lifted 4-7 %).
- 22:3x locometer LM_IEV=1 (interior event log to stderr: planted on stairs sunk or lifted past 3 cm). Building
  lm_d10e / lm_t10e (task btuiyb3pj, waits on memory). Clean-up for the patch: stairDir must be stored as mdw (current
  frame), not scanDir (next-frame) - d10 rotates it by one extra update.
- 22:45 HEAD moved: 10982b3 = snapshot 55 (my stance-crossfade fix; animator b251d0ea / character.h 1628ef2e are HEAD
  now), 1a83e24 = 54. tests/anim/anim_test.cpp modified in the tree by the faces agent (lip colour) - not mine.
  Pivot patch vs HEAD: qs/pivot_animator_b251d0ea.patch (git apply --check OK). Blob 002915d0 NOT yet hash-object -w'd.
- Interior diagnosis with LM_IEV (iev.py <err> [minframes] groups episodes and says where on the stair):
  (a) tight switchback up: the foot landing on the landing during the turn onto flight 2 (body yaw -97, turning
      2.4 rad/s) with its toes 18 cm into flight 2's first riser for a whole stance: the swing scan ran along the
      predicted WAY at landing (mdw, 39 deg ahead of the body) while the foot landed 18 deg ahead of the body - the
      1-D fit put it on the tread "heel overhanging" along the scan line, but the real foot (20 deg off the scan line)
      straddled the riser. FIX in dtree11: scan (and shift) along the foot's own axis at landing (fyaw + turnOver).
  (b) the last floor step before a flight: heel 20 cm before the first riser, toes 6 cm into it at toe-off; the
      landing probe (mid + 0.4 L = heel + 0.9 L) missed the riser -> no scan. FIX dtree11: probe at mid + 0.65 L.
  (c) DOWN flights: planted with the ball 3-6 cm past the nose (heel only on the tread) -> at toe-off the foot pivots
      about the ball in the air and the whole foot lifts 4-7 cm (the "lifted" 4-7 %). The fit going down needs
      heel margin (mg + 4 cm) + L + mg = 32.5 cm of tread: never fits a 27-28 cm tread, so it took lo (heel margin)
      leaving the ball at the nose; landings then run 3-6 cm long. FIX dtree11: split the shortfall (uh = (lo+hi)/2).
  (d) spirals: the harness's pure pursuit (0.6 m ahead) cut far inside R 0.55 (inner heel at r 0.25 on 13 cm treads,
      turn 2.4 rad/s) - harness artifact. locometer now follows kind 6 by tangent + 3x cross-track (LM_PURSUIT=1 old).
  (e) PACE: the game's walkers keep their flat pace on stairs (block_stairs: 1.2-1.4 m/s; pedai has nothing for
      stairs). The walk's step at 1.0 m/s is 0.65 m (walkCycle: sqrt(60 v 0.007058)), so at 1.0-1.4 m/s they climb 2-3
      treads a step (harness plants: 0.52-0.81 m apart). One tread a step needs ~0.45-0.5 m/s (the slow walk's cadence
      1.54 steps/s below 1.0 m/s). -> tell main: AI should slow peds on stairs; harness LM_IV=<pace> for the stairs.
- locometer additions (22:3x-22:50): runIS() split out of interior() (struct IS at file scope); kit() sweep (LM_KIT=1:
  kit.straight.r*_t*, kit.switchback.w*_l*, kit.lstair.w*, kit.winder.w*, kit.spiral.R*_a*_r*, kit.curved.R*; up/down);
  kind 3 (straight flight) in runIS; rooms report half_width_hands_cm / half_width_feet_cm; LM_IV pace override.
  Backup before: locometer_pre_kit.cpp. job_d11 (task bgh0pleck): builds lm_d11 / lm_d10e / lm_t10e, full suites
  r9/fs_{c10,d10,d11}_p{0,2}_{30,60}.txt.
- 23:30 F3 SIGN-OFF SENT to main (blob 002915d0 written with hash-object -w; patch qs/pivot_animator_b251d0ea.patch;
  st15s locostairs unchanged vs st14s: up sunk3 1.1->1.0 %, deep 161->158). Also told main: stair PACE finding, AI
  should slow to ~0.5 up / 0.55 down BUT only after my slow-pace fix lands.
- SLOW-PACE stairs (LM_IV=0.5): sunk3 15-26 % (vs 1-4 % at 1 m/s). Cause: moveW = v below 1 m/s (walk blended with
  idle): the blended swing is low -> early touchdown (swingU > 0.7 && touching), graze hold mid-swing (late = any swing
  when moveW < 0.95), toe-off hold kept until the pose has swung ~45 cm ahead (pc 0.45 at release), the swing fade
  stopped when the pose foot < 1.2 cm (frozen 7.7 cm correction) -> landing 10-13 cm short of the fit.
  dtree12 = dtree11 + blendLift[s] (= the walk-blend lift the shown foot gets: (1-moveW)*0.07*scale*sin(pi swingU))
  counted in all 4 tests (switches LM_BL_TOUCH0 / LM_BL_GRAZE0 / LM_BL_HOLD0 / LM_BL_FADE0, all: LM_TOUCHLIFT0).
  Result 0.5 m/s: switchback.up sunk3 16->4 %, tight 22->1, lstair.up 15->2, winder.up 18->4, downs 21->0-4,
  curved 23->0, slides max 86->24. Pace 1.0: unchanged. REGRESSION: flat.walk_slow_0.9 0.21->0.71 cm/step,
  agait.0.9 0.22->0.64, start.walk_1.4 floats 0->36 - comes from the GRAZE part (without it the 0.9 regression goes;
  start scuffs 2->22 then). Next: make the flat case clean (stairs-only gating? threshold?). Also debug dbgDrift/dbgPred
  = the pose's landing mid along md (m) / its lead (strides) at each plant (trace dr/pr): 0.5 m/s lead 0.30 (R foot)
  vs 0.37 (L foot); 1.0 m/s 0.315. strikeLead is only learned at moveW > 0.9.
- harness: flat.walk_stroll_0.5 / _0.7 added (+ .swing_corr_mean_cm / _max_cm). job_d12 (bwbmlx03j) rebuilds
  lm_d12 / lm_d11 / lm_t10e + full suites r9/fs_{c10,d11,d12}_p*.
- d11 vs d10 at pace 1.0: downs much better (lifted 4-6 -> 0.7-2.3 %, floats /5) = the descent split; ups slightly
  worse (switchback.up sunk3 0.6->2.2, lstair.up 3.6->4.2) - to separate (LM_FAXIS0 / LM_PROBE04 / LM_SPLIT0).
- 23:3x MAIN: F3 (pivot, blob 002915d0) ACCEPTED -> snapshot 57 (with the city agent's terrain bake, after showcase
  set 4). Do NOT graft; main puts it in after 57 commits. Stair pace relayed to the AI agent (0.5 up / 0.55 down) but
  HELD until my slow-pace fix lands. Order: slow-pace fix (clear the 0.9 m/s flat regression first) -> turn onto a
  flight -> building-kit stair spec.
- 2026-10-20 ~00:30 MAIN: snapshot 57 committed (c9ff700, PR #50) with my heel pivot: shared tree animator.cpp =
  002915d0 (= clean10). BASE THE SLOW-PACE STAIR FIX ON 002915d0. When it lands, main gives the AI agent the go for the
  stair pace cap.
- dtree12 (debug, all switches) = d10 (curved landing + rescans, stairDir) + d11 (foot-axis scans LM_FAXIS0, landing
  probe at mid+0.65L LM_PROBE04, descent split LM_SPLIT0) + d12: blendLift steady-gated (|in.speed - speedS| < 0.05..0.2)
  counted in TOUCH (blendLand: off from swingU 0.7..0.85) / GRAZE (blendLand) / HOLD / FADE / STAY (planted & touching
  after toe-off) [LM_BL_*0, LM_TOUCHLIFT0, LM_BL_STEADY0, LM_BL_LANDU]; late-stance landing probe going up (footR<-0.05,
  ph > duty-0.08 or swingU>0, stairSeen<1.5; held dzRoot only from near probes) [LM_EARLYPROBE0]; atFit (a swing's tread
  fit only if the shown heel is within 0.3*scale of the fitted heel along stairDir) [LM_ATFIT0, LM_ATFIT_TOL]; liftGround
  (new field: a swing keeps above the ground it left until swingU 0.35..0.6 on stairs) [LM_LIFTFLOOR0]. Each step
  checked with qs/vmat.sh (full suites p0_30/p2_60 + interior at pace and at 0.5) -> r9/vm_<tag>_*.
  d12i (all on) vs c10: see below. Kit sweep r9/kit_d12h_iv5.txt (kittab.py) - spirals need >= 0.28 m tread at the walk
  line and R >= 0.55; straight treads >= 0.26; winders W >= 1.2 best; curved R >= 2 fine; switchback landings 0.8-1.4 ok.
- 00:55 CANDIDATE "stairpace": patches/stairpace.py on clean10 (002915d0 = HEAD c9ff700) -> clean11/animator_qs11.cpp
  = 503a5886, clean11/character_qs11.h = acdfc600 (HEAD 1628ef2e + stairDir[2], liftGround[2], blendSteady after
  stairLand). = dtree12 with defaults + LM_STEADYUP=10 (bit-identical metrics, vm_su10 vs vm_c11c). Additions since d12i:
  lateSt also when the other foot is planted a step higher (fixes the top-step-onto-landing scrape, kit straight max
  17 -> 0.3 cm); blendSteady (the steady gate held: rises 10/s, drops 12/s; replay ped 12 scuffs 57 -> 33, c10 39).
  Harness: ctree11 / sl/lm_c11. Full suites vs c10: 24-30 worse / 186-199 better (r9/fs_c11c_*). Kit sweep at 0.5 m/s
  (kit_c11c_iv5*.txt; kittab.py): c10 16-26 % sunk3 in every family -> c11 0.7-1.4 (straight/switchback/L/curved), winder
  4.2, spiral 9.2 (median 4.5). anim_test: earlier version ALL PASSED; final rerun at12.sh pending (bo3q7wvb6).
- In game: orch32.sh (00:51, detached): st16a = git archive c9ff700 + st15's test blocks (+ LOCO_STAIRPACE env in
  lsSend) ; st16 = st16a + clean11 (503a5886/acdfc600). Runs: locostairs pace 0.5 (st16p, st16ap), own pace (st16s,
  st16as), locotour traced (st16t, st16at). ~5 h. Compare: lsum.sh log_st16ap.txt log_st16p.txt etc.; tourcmp.py.
- 01:25 KIT SPEC SENT to main (text in loco/kit_spec.txt; numbers from r9/kit_c11c_iv5.txt + flat/room half_width
  measures). Status sent: candidate 503a5886/acdfc600, anim_test ALL PASSED (sl/at_qs11.log), in-game pending (orch32).
- Tried and rejected: ball probe whenever the fitted heel is within 3 cm of the nose (LM_BALL5 in dtree12): mixed
  (ups worse at 1 m/s). dtree12 source now has LM_STEADYUP default 10 and the LM_BALL5 switch (default = new, i.e.
  dtree12's default differs from the candidate: run lm_d12 with LM_BALL5=1 to match the candidate).
- Open after the sign-off: winders at walk line 0.5 (feet span 2 wedges; scan direction jumps while the foot turns
  44 deg in its swing), spirals under 0.5 m (narrow treads: would need a turned foot), fast-pace landing turn on a
  tight switchback (shallow-angle straddle), stops on stairs (fixed the 42 cm hold with atFit), flat stair-walk at the
  game's current fast pace 2-3 treads a step (AI pace cap pending).
- 01:30 MAIN: kit spec passed to the city agent (building kit rules, worldv checks, doc v1.1). Sign-off of the stair
  fix must include: blobs on 002915d0, harness numbers, locostairs at 0.5 m/s AND at the walkers' own pace, locotour.
  Then main gives the AI agent the go for the stair pace cap.
- Sign-off files ready: blobs 503a5886 / acdfc600 (hash-object -w done), patches qs/stairpace_animator_002915d0.patch
  (+117/-30) and qs/stairpace_character_1628ef2e.patch (git apply --check OK), graft text qs/stairpace_graft.txt.
- Tried, rejected (dtree12 switches, defaults = candidate): LM_TRS=0.15 (smoothed turn rate for landing prediction:
  fs 49/22 worse/better, iv5 28/8), LM_TURNNOOVER=1 (no heel overhang on slanted landings: switchback.up sunk3 2.2 ->
  7.2), LM_BALL5=1 (ball probe near the nose: mixed). dtree12 with defaults == candidate (vm_d12n == vm_trs0).
- Next ideas: spirals - the inner foot lands 0.11-0.19 m inside the walk line on 16 cm treads (R0.5); shift the inner
  foot's landing outward in tight turns on stairs (needs a lateral component in the tread move: stairLat).
- 01:33 orch32: st16 (503a5886/acdfc600, verified hash) and st16a (002915d0/1628ef2e) built; run st16p started 01:33.
  Order: st16p, st16ap (pace 0.5), st16s, st16as (own pace), st16t, st16at (tour). Monitor baoj7d2fd on orch32.log.
  at12.sh rerun: ALL PASSED (sl/at_qs11.log).
- LM_SPLAT prototype (dtree12, default 0; NOT in the candidate): the inner foot's swing landing moved outward by
  min(0.01*k*curv, 0.07)*scale across the turned walk direction on stairs (stairSeen < 1.5), curv = |turnRate|/max(spd,
  0.3); A.stairLat[s] (new debug field) carried with qBack, reset with the fit, added to the fade target and the probe.
  vs candidate: k=3 fs p0_30 21/23 worse/better, p2_60 23/42, iv 23/39, iv5 20/20, kit spiral 113/247, winder 16/28;
  k=6 fs 24/24, 29/44, iv 26/41, iv5 24/23, spiral 161/242, winder 13/35. Spiral DOWN much better (R0.5 a22.5 sunk3
  39 -> 21 (k3) -> 11 (k6)), spiral UP a bit worse (sunk3 +1..+7, slide max +1..+9 cm) -> make it descents-only or
  ascent-tuned before using it (item 4, after the turn-onto-a-flight and curves work).
- 01:45 FOUND (harness, candidate c11): iv5 (0.5 m/s) p1 run 1 first flight (all 4 up scenarios, t 8.433): a 62-frame
  full-riser sink (heel on tread k, ball/toes 17 cm into riser k+1) - NEW vs c10. Cause (LM_DBGSCAN prints in dtree12,
  lm_d12s): the swing fit put the heel 4.2 cm over the nose (non-ball, limit 5), the slow walk touched down early
  (swingU 0.78) 4.1 cm short -> heel 8.3 cm over -> the mid probe (heel + L/2) at the riser line found the lower tread
  -> the held foot took it. Plant d (actual - fitted heel) iv5: median 0, p5 -3.3, p95 +4.2 cm.
  Turn case (p2_60, switchback.up p3 run 3 t 7.417, 49 fr): ball fit at the overhang limit (lo = r+mg-0.7L), graze
  HOLD at swingU 0.87 5.6 cm short -> the ball at the real riser -> ball probe found the lower tread, toes 17 cm in.
  Fix under test (dtree12 -> lm_d13): new field stairRear[2] (fitted heel's gap ahead of the lower nose behind its
  tread); overNose(s) at the plant and the graze hold: ballProbe also when stairRear + d < -0.05 [LM_PLANTBALL0 off];
  LM_BALLFWD=<cm> moves the ball probe toward the toes (default 0).
- 02:00 CANDIDATE 2 "stairpace + forefoot": patches/stairpace.py now has hunk 11 (overNose: a swing coming down short
  of its fit, heel > 5 cm over the nose behind its tread -> ballProbe; new field stairRear[2]). Old script kept as
  patches/stairpace_c11.py. clean12/animator_qs12.cpp = 63a9f73e, clean12/character_qs12.h = 9dfb632f (hash-object -w
  done); patches qs/stairpace12_animator_002915d0.patch (+133/-32), qs/stairpace12_character_1628ef2e.patch (apply
  --check OK on HEAD 2b3102e = merge of PR #50, same content as c9ff700); graft text qs/stairpace12_graft.txt.
  ctree12 / sl/lm_c12 (== lm_d13 default bit-identical). anim_test attree13: ALL PASSED (sl/at_qs12.log).
  c12 vs c10: fs 23-29 worse / 190-201 better; iv 18/106; iv5 15/172. c12 vs c11: 0-1 worse / 2-13 better.
  lm_c10 rebuilt from ctree10 with the current harness (fs identical to the old fs_c10 files): r9/vm_c10n_*,
  r9/kit_c10n_{iv5,pace}.txt, r9/fs_c10n_*. c12: r9/vm_c12_*, r9/fs_c12_*, kit = r9/kit_d13_{iv5,pace}.txt.
- In game: orch32 STOPPED 01:58 (kill 8050) after starting st16p (old candidate, still running, PID 22319 -> log_st16p).
  orch33.sh (detached): builds st17 (= st16a + clean12), waits for st16p, then st17p, st16ap (0.5), st17s, st16as (own
  pace), st17t, st16at (tour, traced 13,35,12,114,107). Monitor b24w3srg7.
- Tried (dtree12 switches, default off): LM_BALLFWD=3 (ball probe 3 cm toward the toes): landing-turn sinks at 1.0 m/s
  switchback.up 52 -> 3, tight 97 -> 2 frames, but slides worse (switchback_tight.up max 4 -> 9, spiral_tight iv5 1.2 ->
  6.7); LM_BPFLAT (flatten a ball-probe foot about its ball): much worse; LM_EDGEKEEP (planted foot kept with the world
  when its probe reads > 6 cm off after 0.1 s): spiral.down_0.7 slide max 40 -> 1.7 but spiral_tight.up sunk3 8 -> 23.
  Known: c12 spiral.down_0.7 (own pace) one 40 cm slide = a planted foot pivoting with its probe point over the outer
  edge (probe -0.40) -> dzRoot +0.25 spike at toe-off -> hold released, correction faded on the ground.

## 23. 2026-10-20 02:27 RESUMED after the ~02:19 container restart (new animation agent, from these notes)
- HEAD f595703 (PR #51 faces/doc on top of 2b3102e); HEAD animator = 002915d0, character.h = 1628ef2e (= my bases).
  Candidate unchanged: clean12 animator 63a9f73e / character.h 9dfb632f (blobs in the object DB; st17 tree holds them).
- orch33 before the restart: st17 built (02:08), st16p (old c11 at 0.5) FINISHED (log_st16p.txt, trips 7, peds cpu
  2.05); st17p was running (killed; partial run_st17p.killed.txt). Nothing else ran.
- orch34.sh (detached, PID in locotour/orch34.pid, log orch34.log): st17p, st16ap (0.5), st17s, st16as (own pace),
  st17t, st16at (tour, traced 13,35,12,114,107); each run gated: no hold, no fresh /tmp/neontide_lead_wants, memfree
  >= 3300. Skips a tag whose log is complete. ~37 min a run.
- 02:55 st17p DONE (log_st17p.txt): same as st16p (c11) to the decimal except lifted-up 1.1 -> 0.9 % (the forefoot rule
  touches few frames in game). 0.5 m/s: up sunk3 2.4 %, down 2.8 %, slide mean 0.68 / 0.48, max 38 / 56 cm, bigslide 8
  (all swing feet skimming a tread, planted 0 hold 0, some quick steps). st16ap (base, 0.5) running from 02:55.
- INTERIOR WORK (after c12), harness additions: debug tree qs/dtree15 = ctree12 (c12 exactly) + patches/dbg15.py prints
  (Anim::gAnimDbg, set by locometer for the LM_TRACE run via #ifdef ANIM_DBG_HOOK: SCAN fits, PLANT, HOLD, QSTEP,
  STEPEND, WALK per frame, SREQ scan requests) -> sl/lm_d15 (default == lm_c12 bit for bit, checked). Tools: why.py
  <trace> <t0> <foot> [back] (events before a failing plant), ievcat.py <glob> (episodes by where: floor/f1/land/f2),
  qs/evalv.sh <bin> <tag> [ENV] (fs p0_30/p2_60 + interior survey 3 prof x 2 Hz x own/0.5 with IEV + straight stairs at
  0.5 = r9/ev_<tag>_*), evsum.py <tagA> <tagB>; cgate.sh (compile gate 2500 MB + lead flag, niced), mk2.sh <tree> <out>.
  locometer: LM_IREP=n (interior: each person n times, other seed / start 7 cm back / pace +-4 %; backup
  locometer_pre_irep.cpp); evalv2.sh / evsum2.py (EVP=ev2) use LM_IREP=4 for the survey.
- CAUSES found (c12, 0.5 m/s, all profiles): (1) switchback.up: in the slow tight turn on the landing the planted
  foot's correction grows to the reach limit (body turning over the world-fixed foot, +pivot); the quick step fires at
  swingU 0.1-0.15 (foot kept down by the slow walk's stay rule), ends inside the gait's swing, is released at once (a
  20 cm jump up), and the swing lands unfitted (its fit was cleared in the step, no late scan; stairSeen > 1.5 on the
  landing too) -> forefoot under flight 2's first tread. (2) late re-scans (swingU 0.72-0.86) moving the fit to another
  tread (+-18-23 cm) the swing cannot reach; atFit (27 cm) still trusts it. (3) winders: a wedge shorter than the foot
  along the scan -> fit falls back to toes-short (uh = hi) leaving the ball off the wedge -> ball probe reads the lower
  tread. (4) descents: the scan's first run has no known rear edge -> fit pulls back 16 cm with the heel at the riser
  behind (1-4 cm overlap). (5) own pace: shallow-angle straddle on a tight landing turn: ball 0.7 cm short of the nose,
  toe tip ~2 cm in a riser (metric counts a full riser for a 2 cm toe overlap).
- PROTOTYPES (dtree15 switches): LM_FIXQS (reach limit past the stance -> toe-off hold, the gait's swing takes it):
  0.5: switchback.up sunk3 1.21 -> 0.34 %, floats 16 -> 0, tight.down slide max 12.5 -> 2.9; straight stairs 0.5 16
  better / 0 worse; flat turns mixed (floats better, pivot/slide p90 +0.1-0.2). LM_FIXSW=0.15 (late re-scan pays to
  change tread): lstair.up own 3.09 -> 2.07, 0.5 0.17 -> 0.01; spirals worse. LM_FIXSW2=0.08 (pay only for a jump of the
  fit's shift > 8 cm): gentler, spirals better, fs neutral. LM_FIXR0 (first run's rear margin): REJECTED (straight
  descents worse). LM_FIXLS (late first scan) no effect alone; LM_FIXSEEN (stair window by distance at slow pace): mixed.
  Small numbers of episodes -> judge with LM_IREP=4 (evalv2.sh) next.
- 03:15 BIG FINDING (harness, LM_SV=<pace> on the straight stairs): JOG / RUN on stairs is bad on HEAD and on c12.
  Planted frames sunk 3 cm, 60 Hz p2: jog 3.0 up c10 38-63 % / c12 38-60 %, down 1-30 / 3-38 %; run 5.0 up c10 40-47 %
  (lifted 12-15 %) / c12 67-78 % (lifted 0-2 %), down 22-44 / 26-65 %. (r9/sv_c10_sv*.txt, sv_c12_sv*.txt; 2.0 m/s is
  clean.) Cause (trace r9/tr15_run5.err): the landing prediction uses ONE learned strikeLead (0.3 strides, learned at
  the walk, gain 0.3 a plant) -> a run's plants land 44, 39, 27, 20, 15, 10 cm short of their fits (atFit rejects the
  first, the rest land wrong); also sT clamped at 1.2 m (a run's swing travels more early on: the fit creeps a tread a
  frame). Prototype in dtree15: LM_FIXLEAD (strikeLeadRun, new field, learned and used by sstep(2, 3, speed)),
  LM_FIXST=<m> (the prediction's reach). The player runs stairs all the time: tell main; c12 sign-off must state the
  run-pace trade (fewer floats, more sinks at 5 m/s).
- 03:23 st16ap DONE: in game 0.5 m/s base st16ap -> c12 st17p: sunk3 up 15.9 -> 2.4 %, down 17.9 -> 2.8 %; slide mean/p90
  up 2.88/7.12 -> 0.68/0.55, down 1.15/2.00 -> 0.48/0.53; max 62/65 -> 38/56; bigslide 25 -> 8; cpu 1.75 -> 1.84.
  st17s (own pace) running from 03:22. Sent main a progress note + the run finding.
- LM_FIXLEAD measured (straight stairs, sunk3 up/down 60 Hz): jog 47.6/13.4 -> 1.3/3.8, run 70.6/48.9 -> 4.5/8.0, sprint
  66.5/20.2 -> 16/5.6 (lifted 18.7 -> 1.8); fs 0-1 worse / 0 better (walk untouched). LM_FIXST=2.5 worse (game probe
  window: probes start 0.3 m, scans 0.5 m above the root) -> dropped. Learned lead: walk 0.296 strides (0.44 m), jog
  0.145 (0.31 m), run 0.098 (0.32 m), sprint 0.074 (0.30 m): learn the run's in METRES.
- 4x (evalv2) lead -> lead+QS (r9/ev2_lq_*): 0.5 m/s switchback.up 1.78 -> 1.31 %, slide max 12.9 -> 4.3, floats 31 -> 3;
  tight.down/up slide max 12.5/25.8 -> 2.9/2.7; straight stairs 0.5 0/4, 0/12 worse/better; fs 22/30, 11/30.
- 03:3x MAIN PLAN: (1) sign off c12 as is after own-pace + locotour runs, state the run/jog limitation; (2) NEXT CANDIDATE
  = run lead (LM_FIXLEAD) + switchback-landing quick-step fix (LM_FIXQS), ahead of the other interior items; in-game
  proof: a PLAYER-on-stairs run (jog and run, up and down, straight flight AND switchback) beside HEAD, plus a chase
  crossing stairs if a test mode reaches one; (3) then the remaining interior items. New field additive.

## 24. 2026-10-20 03:41 SECOND RESTART (~03:37): rerun
- Killed: st17s (partial run_st17s.killed2.txt), anim_test of c13. Done before it: st17p, st16ap.
- orch34.sh restarted (log orch34b.log; skips st17p/st16ap): st17s, st16as, st17t, st16at, gated (lead flag fresh at
  03:41: snapshot 60 checks first). anim_test rerun from the surviving binary sl/at_qs13 in attree14 (HEAD 837b39c src +
  clean13) -> sl/at_qs13.log, sl/at_qs13.done.
- CANDIDATE c13 (next): patches/runlead.py on clean12 -> qs/clean13/animator_qs13.cpp = 1a3b712490221531b327dd62e397f4b9f596774a,
  character_qs13.h = bd6d781f29b5bc5692bb04bea5aa798e98098cd9 (NOT yet hash-object -w). = c12 + run lead in metres
  (strikeLeadRun, blended by sstep(2, 3, speed)) + quick-step -> toe-off hold when planted past the stance on stairs
  (stairSeen < 4). Harness tree qs/ctree13, sl/lm_c13 == lm_d15 LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 on all 30 eval files.
  Run paces (r9/run_{base,lead,c13}_<v>[_30].txt): sunk3 up/down 60 Hz jog 47.6/13.4 -> 1.3/3.8, run 70.6/48.9 ->
  4.5/8.0, sprint 66.5/20.2 -> 16/5.6. c13 vs c12+lead: only stair scenarios change (fs 9/13, 6/16 worse/better).
- IN-GAME PROOF to build (main): player on stairs (walk 1.55 / run 4.1 / sprint 6.9 m/s = the player's speeds; stick
  0.49 / 1.0 / sprint), up and down, the metro straight flight AND a test-only switchback (collision boxes added as a
  cell with Phys::gCollision->addCell(key, boxes, {}), invisible), beside a HEAD tree; NPC runners chasing up/down the
  straight flight as the chase proxy (no foot chase test mode exists; "chase" is a car chase). Test blocks: app.cpp only
  (diff c9ff700 -> st16a = 5 hunks, +1026/-2; HEAD changed app.cpp by 18 lines since) -> new trees st19a (HEAD + blocks
  + locoplayer) and st19 (+ c13).
- 03:50 c13 harness set: anim_test ALL PASSED (sl/at_qs13.log; HEAD-only baseline at_head14 on the same tree running:
  check the clothing seed 9902 poke 14 -> 20 comes from HEAD's own change). vm_c13_* / kit_c13_*: vs HEAD fs 26/193,
  30/193, iv 19/111, iv5 15/173 worse/better; vs c12 fs 9/13, 7/16, iv 4/16, iv5 0/12; kit = c12's. Player speeds
  (r9/pl_<bin>_{s,sb}_<v>_<hz>.txt; sunk3 / lifted, c10 -> c13): straight 4.1 up 30.6/9.7 -> 2.7/0.6, down 33.4/6.7 ->
  3.0/3.1; 6.9 up 28.9/17.5 -> 13.9/0.6, down 28.4/21.8 -> 6.0/2.7; switchback 4.1 up 10.8 -> 2.9 (slide max 38.6 ->
  13.7), 6.9 up 12.1 -> 3.1 (max 140 -> 13); walk 1.55 unchanged. Residual: sprint up straight 60 Hz one 57.8 cm
  "slide" (run 21: a swing foot skimming the top landing 5 frames, not planted; HEAD 44.7 too).
- In-game proof trees: locotour/st19a (HEAD 837b39c + testblocks_c9ff700.diff + apply_player.py/block_player.cpp =
  --autoplay locoplayer) and st19 (+ clean13). orch35.sh (log orch35.log): builds both, waits for orch34, then
  locoplayer st19pl/st19apl, locostairs 0.5 st19p/st19ap, own pace st19s/st19as, locotour st19t/st19at.
- anim_test HEAD-only on the same tree (sl/at_head14.log): vs c13 only 4 gait skate maxima at 0.9 m/s move (2 up, 2
  down, within limits); the clothing seed 9902 change came from HEAD itself.
- NEXT ROUND (c14 material, dtree15 switches on top of c13 = LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4), 4x sample:
  + LM_FIXSW=0.15 (late re-scan pays 0.15 to change tread, swingU > 0.5): own lstair.up 1.53 -> 0.88, switchback.up
  1.33 -> 1.11, tight.up 2.38 -> 1.99, spiral.up 4.42 -> 3.12; 0.5 spiral.up 1.02 -> 0.17; fs 19/19, 21/24; s5 2/13,
  4/10 (SW2 jump-variant gentler, weaker: keep SW). + LM_FIXSEEN (stair window by distance at slow pace): 0.5
  switchback.up 1.30 -> 0.09 (slide max 4.3 -> 1.1); fs / s5 no change (r9/ev2_c13swse_*). LM_FIXLS adds nothing.
  Cause of the last 0.5 case: on the landing the stair window (1.5 s) lapsed, the swing toward flight 2 never scanned.
- 03:5x locoplayer block: platform made 3 m deep with end / inner walls (a sprint stop could drop 3.15 m), site check
  centred on the whole 6.6 m footprint, colliders within 6 m. st19a build relaunched (orch35 try 2: killed my own
  stale compile PID 3578 after regenerating app.cpp; the kill counts as a compile error in build.sh -> orch35 exited).
- c14 READY IN HARNESS (after c13): patches/turnflight.py on clean13 -> qs/clean14/animator_qs14.cpp = c427379e (character.h
  = c13's bd6d781f, no header change); = c13 + SW (late re-scan keeps its tread: +0.15 past swingU 0.5) + SEEN (stairs
  seen ~1.8 m of way: dt * clamp(speed/1.2, 0.4, 1)). ctree14 / sl/lm_c14 == lm_d15 + switches on all 30 eval files
  (r9/ev2_c14_*). vs c13 (4x): 0.5 switchback.up 1.31 -> 0.09, spiral.up 1.02 -> 0.17, own lstair.up 1.53 -> 0.79,
  tight.up 2.38 -> 1.91; fs 19/19, 21/24; player speeds ~c13 (noise). Not yet: anim_test, hash-object -w, in game.
- 04:15 queued (background tasks): lm_d15 rebuild (+ LM_FIXWB=1/2 narrow-tread fit variants, + LM_SPLAT/LM_SPLATDN lateral
  shift ported from dtree12; both default off) -> evalv2 c14wb1/c14wb2 + kit winder/spiral (bgyl0dbns) -> qs/job_splat.sh
  (c14sp3dn/sp6dn/sp3/sp6 + kit spiral/winder/curved, bfd5ej93s); c14 anim_test qs/at15.sh -> sl/at_qs14.log (attree15).
  st19a build fixed: `near` is a Windows macro (MinGW) -> renamed nearby (orch35 try 3, log orch35.log).
- c12 SIGN-OFF DRAFT: loco/qs/signoff_c12_draft.txt (fill OWNPACE / TOUR from st17s/st16as/st17t/st16at, then send).
- 04:2x c14 anim_test ALL PASSED (sl/at_qs14.log == at_qs13 but timing). st19a built (locoplayer compiles); st19 building.
  st17s slow (game time 730 s at 04:20: load).
- Narrow-tread fit on top of c14 (4x): LM_FIXWB=1 (skip the tread) WORSE (winder.up 0.5 2.73 -> 3.76, spiral_tight.up
  6.8 -> 15.4) -> rejected. LM_FIXWB=2 (a too-short tread going up keeps the ball on it: lo = hi = r + mg - L when
  hi + L < r + mg): winder.up 0.5 2.73 -> 1.42 (slide max 8.9 -> 2.2), own 1.47 -> 1.35; spiral_tight.up own 8.2 ->
  6.2; fs 2/2, 5/5; kit 0.5 winders up 2.90 -> 1.51, spirals neutral (up 2.37 -> 2.46; down 13.3 unchanged) -> KEEP for
  the curves round. Descents (winder 4.1, spiral 13.3 at 0.5) next: LM_SPLAT job running.
- c13 patches made: qs/runlead_{animator_63a9f73e,character_9dfb632f}.patch (on c12) and
  qs/c13_{animator_002915d0,character_1628ef2e}.patch (on HEAD; git apply --check OK).
- 04:33 LM_SPLAT on top of c14 (4x, r9/ev2_c14sp*): descents only k=3 (LM_SPLAT=3 LM_SPLATDN=1) best: spiral_tight.down
  sunk3 own 24.7 -> 14.0, 0.5 23.3 -> 13.9; spiral.down 4.0 -> 3.3 / 1.9 -> 1.3; winder.down 0.5 2.9 -> 2.3; BUT floats
  up (tight.down lifted 4.4 -> 6.6 % own, 5.5 -> 7.7 % 0.5; winder.down 2.8 -> 3.2 %); fs 7/12, 10/20. k=6: tight.down
  own slide max 16.5 -> 46.8. Both ways: spiral.up 0.5 0.17 -> 2.5 (k3) -> keep descents-only; find why floats grow
  before using it (curves round, after c14).
- locotour/block_player.cpp v2 (for c14's in-game proof; st19/st19a were made from v1 = block_player_v1.cpp): after
  the player's segments, LOCO_NPCSWITCH=n (<= 4) walkers walk the test switchback up/down 2 min at LOCO_STAIRPACE
  (default 0.5), right-hand lanes, measured into "locoplayer switchback up/down npc" lines. Not compiled yet.
- Lifted episodes with SPLAT dn on tight spirals: toe-off of a foot whose forefoot overhangs the nose going down (heel
  7 cm up, ball 20 cm over the lower tread) - known descent pattern; net sinks -5178 frames vs lifts +249 (0.5, 4x).
- c15 (curves round, after c14): patches/curves.py on clean14 -> qs/clean15/animator_qs15.cpp = e30f2665,
  character_qs15.h = 3d37b201 (new additive field stairLat[2] after stairRear) = c14 + narrow tread going up keeps the
  ball on it (WB2) + descents-only lateral shift of the inner foot on tight bends (3 cm per 1/m curvature, max 7 cm).
  ctree15 / sl/lm_c15 building + equivalence check vs lm_d15 switches (r9/ev2_c15*, task bub702qyu).
- 04:38 st19 + st19a exes built (c13 proof); orch35 waits for orch34 (st17s at test t 70 / 108, slow: load ~13).
- 04:52 st17s DONE (log_st17s.txt): up sunk3 3.2 %, down 5.9 %, trips 14 (st15s on an older build had 27: the newer AI
  congests the stair - walkers at 0.05-0.5 m/s), peds cpu 2.89. Compare ONLY with st16as (running from 04:51).
- c15 clean == debug (0 diffs). c14 -> c15 (4x): winders 0.5 up 2.73 -> 1.42, down 2.92 -> 2.33; spiral.down 1.90 ->
  1.33; tight.down 23.3 -> 13.9; fs 9/14, 15/26; but straight crowd.down sunk3 2.6 -> 3.4 (the weave's turn rate fires
  the shift). LM_SPLATS=<tau> (smoothed turn-rate gate, debug field turnRateS): 0.6 s removes the straight regression,
  gives back part of the gains (tight.down -> 15.2, winder.down -> 2.59); 1.0 s similar. Decide in the curves round.

## 25. 2026-10-20 05:20 THIRD RESTART (~05:15): rerun; c13b = c13 with the hand-over only early in the swing
- HEAD now bcb8a71 (snapshot 60: world format / validator; no anim, peds, player or tests/anim change since 837b39c);
  anim blobs at HEAD still 002915d0 / 1628ef2e.
- st16as (HEAD own pace) was SIGKILLed at 05:07 (exit 137, OOM?) before the restart; logs kept as
  locotour/{log,run}_st16as.killed3.txt. orch34 relaunched (log orch34c.log: has st17p, st16ap, st17s; runs st16as,
  st17t, st16at). orch35 relaunched (orch35.log; tries 1-3 in orch35_try*.log): st19a exe ok, st19 rebuilds with c13b.
- c13 BUG FOUND IN REVIEW (fixed = c13b): the stairs hand-over (reach limit: a planted foot is released to footHold)
  also fired late in the swing; there the other foot touches down at once, the limit trips again -> planted/held
  toggled every frame -> repeated footEvents -> repeated footstep sounds (550 of 726 hand-overs were past swingU 0.5:
  straight-stair stops / blocked walkers). Fix: only while swingU < 0.5 (debug switch LM_FIXQSU=0.5). Harness metrics
  unchanged (fs 0/0 diffs); late hand-overs 0 (r9/ev2_c13b_* = lm_d15 + LM_FIXLEAD=1 LM_FIXQS=1 LM_FIXQSG=4 LM_FIXQSU=0.5).
- c13b blobs: animator 0869189d2d2cb984f037b849326561449b7826f6 (qs/clean13b/animator_qs13b.cpp), character.h
  unchanged bd6d781f29b5bc5692bb04bea5aa798e98098cd9 (strikeLeadRun). c14b = b82e6e92225a5b3d8b777773d34bb4abdd1d504a (header
  = c13b's); c15b = 58fa4d0f79dfb5eb2e45e854bc21437c27d8cbee / 3d37b20132da00fe9e4eb42810e426209a777756. Old c13 / c14 / c15
  blobs superseded. ctree13/14/15 + st19 now carry the b animators; old st19 exe -> bin/loco_st19_c13a.exe.
- TODO: qs/job_b.sh (log qs/job_b.log): rebuild lm_c13/14/15 from the b trees, check each == lm_d15 + switches, rerun
  c13b anim_test (attree14 -> sl/at_qs13b.log); regenerate qs/runlead_* and qs/c13_* patches for c13b; hash-object -w;
  fix qs/signoff_c13_draft.txt (blob 0869189d, patches).
- 05:25 comment wording fixed in the c13b hand-over (no code change) BEFORE any b build started -> FINAL b blobs:
  c13b animator 24831ff311bcfda686c5d963f2172eb7c017aa6e (written -w), header bd6d781f; c14b 07d29e18a9e8469412751f57590ed386410498af;
  c15b 16e6ee2ac6cddb8a8b407b062a016c99643e90f1 / 3d37b201. (0869189d / b82e6e92 / 58fa4d0f superseded.) Patches regenerated:
  qs/c13_animator_002915d0.patch + qs/c13_character_1628ef2e.patch (on HEAD) and qs/runlead_*.patch (on c12) both
  reproduce 24831ff3 / bd6d781f (checked on a HEAD archive). Old c13a patches in qs/old13a/.
- 05:28 st19 (c13b) app.cpp switched to locoplayer block v2 BEFORE its build (v1 copy: locotour/app_st19_v1.cpp):
  without LOCO_NPCSWITCH it runs exactly v1's segments, so st19 vs st19a (v1, built 04:19) is a fair c13 proof, and
  st19 with LOCO_NPCSWITCH=4 is c14's "before" on the switchback at 0.5 -> c14's proof needs one build only (st20 =
  st19 tree with c14b 07d29e18). job_b.sh reniced to 15 (the game runs at nice 6; harness evals at nice 4 would beat it).
- 05:5x spiral.down_0.7 big "slides" (66.5 cm sv p2_60 own, 39.8 cm fs p2_60; same in c12 / c13b / c14) = HARNESS EDGE
  ARTIFACT: the outer foot walks at r 0.85 = the spiral's open outer edge (rOut 0.85, walking line 0.55, no wall in the
  harness); its heel goes over the void (ground = the floor below), and as it swings back over the tread it is inside
  the tread above for a few frames (counted as contact). The float max 600 / 1200 mm on that scene is the same case
  (a sole point over the edge or the column). Not an animator target; a real spiral has a wall there.
- 05:46 st16as started (orch34c), st19 build started (orch35, block v2 + c13b). job_c (c14b anim_test) and job_d (c15c
  = c15b + bend smoothed 0.6 s + stairLat resets; patches/curves2.py; qs/clean15c; ctree15c) queued behind job_b.
- 05:50 c14b blob written (07d29e18); patches qs/turnflight_animator_24831ff3.patch (on c13b, +8/-3) and
  qs/c14_animator_002915d0.patch (on HEAD; with qs/c13_character_1628ef2e.patch) both reproduce 07d29e18 / bd6d781f.
  Draft: qs/signoff_c14_draft.txt (ATRESULT, NPCSWITCH, PLAYER to fill). orch36.sh (log orch36.log) launched: builds
  st20 (= st19 tree + c14b), waits for orch35, then st19n / st20n (locoplayer LOCO_PLAYERLAPS=1 LOCO_NPCSWITCH=4),
  st20pl (as st19pl), st20p (locostairs 0.5 as st19p). If time is short, kill orch35 (PID in orch35.pid) before its
  tour pair (st19t / st19at: c13 does not change flat walking) so orch36 can start.
- 05:58 st19 BUILT (block v2 + c13b, 05:57). Memory squeeze: 3 games at once (mine st16a, nt_e2, nt_ai103; all oom adj
  500, scores ~1130) + a build -> memfree ~1000: killed my own lm_c15 harness compile (only for c15b, superseded by
  c15c anyway; job_b's "lm_c15 vs debug" line uses the stale 04:40 binary -> ignore it; job_d builds lm_c15c).
- NOTE for the curves round: harness spiral.*_0.7 = the kit's RECOMMENDED spiral (docs/HANDBUILT_WORLD.md: 1.6-2.0 m
  across, walking line 0.55-0.6, 26-30 deg). Its outer foot reaching r 0.8-0.87 (rOut 0.85: the outer wall in a real
  building) means feet clipping into the outer wall in game: in that trace the feet were 0.34 m apart across (inner
  r 0.46, outer 0.80; body ~0.63, outside the line). Check whether the body drifts out or the outer foot lands wide.
- c13b vs old c13 metrics: 13 of 30 eval files differ, all tiny (evsum2 c13 c13b: fs 0/0, s5 0/0; interior only
  switchback_tight.up own slmax 63.6 -> 67.0, sk3 2.38 -> 2.40). c12 -> c13b counts: fs 9/13 and 6/16, ivall own 11/19,
  0.5 2/29, s5 0/4, 0/12. job_e.sh (log qs/job_e.log) redoes vm / pl / run files for c13b -> fix the draft's numbers.
- 06:12 anim_test ALL PASSED for c13b (sl/at_qs13b.log; vs HEAD at_head14.log only the 4 gait skate maxima at 0.9 m/s)
  and c14b (sl/at_qs14b.log; == c13b's but timings). c13b == lm_d15 + switches (0 diffs), c14b likewise (0 diffs).
  c13b harness for the draft redone (job_e): player speeds == c13 to the decimal (qs/plsum.py lm_c10 lm_c13 lm_c13b),
  vm counts vs HEAD unchanged (26/193, 30/193, 19/111, 15/173); c12 -> c13b fs 9/13 and 6/16 (draft fixed).
- c14 SW at run speeds is MIXED (pl_dbg_{b13,sw,sws}_*: straight 4.1 60 Hz up lifted 0.6 -> 2.2 %, switchback 4.1 30 Hz
  up 3.0 -> 4.2, 6.9 30 Hz 4.3 -> 5.6; but switchback 4.1 60 Hz 2.9 -> 1.5, straight 6.9 30 Hz 10.3 -> 9.5). SEEN no
  effect there (as designed). Testing LM_FIXSWW=1 (SW faded out 2 -> 3 m/s with runK) in dtree15: qs/job_f.sh.
- 06:2x LM_FIXSWW=1 (dtree15: SW cost * (1 - runK)) result: player speeds == c13 again (pl_dbg_sww_*), walking
  identical to c14 (ev2_c14w vs c14bdbg: fs 0/0, s5 0/0, interior 0/0). -> c14c = c14b + patches/turnflight2.py (cost
  0.15 * (1 - runK)), qs/clean14c (header = c13b's). c15d = c14c + curves.py + curves2.py (qs/clean15d; header =
  c15c's 3d37b201+turnRateS). NOTE ctrees use the DEBUG-SUPERSET character.h (all fields + dbg*): never copy a clean
  header into a ctree (fixed ctree15c / ctree15d). job_d (c15c) stopped; job_g.sh: lm_c14c / lm_c15d + checks
  (c14w, c15ddbg), player speeds c14c, kit sweeps c14c / c15d.
- 06:26 st16as DONE (HEAD own pace) -> c12 OWNPACE filled in qs/signoff_c12_draft.txt: sunk3 up 7.9 -> 3.2 %, down
  7.2 -> 5.9 %; lifted 1.2 -> 1.1 / 4.1 -> 3.5; slides ~same; lowest sole -252/-177 -> -169/-169 mm; bigslides 9 -> 9
  (all free swing feet grazing a nose); cpu 2.69 -> 2.89. orch34 now runs st17t then st16at (tour, ~1 h each).
- st20 build started 06:24 (c14c 4c1b658f + block v2).
- 06:3x c14c blob WRITTEN 4c1b658fe4bbebd33b7da616131bdd54ded77748 (st20 tree has it; st20 build started 06:24 with it);
  patches regenerated for c14c (turnflight_animator_24831ff3.patch +8/-3; c14_animator_002915d0.patch), both reproduce.
  lm_c14c == debug c14w (0 diffs). Draft qs/signoff_c14_draft.txt updated (blob, HOW fade, player speeds).
- STOP FLOAT (pre-existing in HEAD / c12 / c13: stairs17.stops.up float max 161-166 mm): a walker stopping on a flight
  going up: the late fit lands two treads up (sh 0.13-0.18, land +0.34), the foot comes down 18-25 cm short
  (PLANT d -0.177, atFit tolerance 0.3) and overNose stands it on its forefoot AT THE FIT'S HEIGHT, the ball 7 cm behind
  that nose -> 15-33 cm in the air (traces qs/stk6.err, stk9.err; harness LM_TRACE=k on "stairs17.stops", runs 0, 6,
  15, 18 on c13b). c14's SW added one more (run 9: keeps that far fit; stops.down max 43 -> 150 mm at 60 Hz).
  Debug switches added to dtree15: LM_FIXSWUP=1|2 (SW only for a tread up / root rising), LM_FIXSWMAX=m (SW as a bonus
  for the kept tread only while it needs <= m shift), LM_FIXATB=m (a fit with a nose behind holds only while the ball
  is >= m past that nose). Jobs: job_h (SWUP), job_i (SWMAX .08/.10/.12), job_j (ATB on c13b and c14c).
- 06:4x c15d (= c14c + curves + curves2) release == debug c15ddbg (0 diffs). vs c14c: kit spiral down 0.5 mean 13.3 ->
  8.0 % (24 better / 2 worse), pace 13.6 -> 6.4 (26/0); spiral up pace 6.3 -> 4.5; winder up 0.5 2.90 -> 1.51 (qs/kitcmp.py
  c14c c15d). Interior: tight.down own 24.7 -> 16.2, 0.5 23.3 -> 15.2; spiral.down 4.03 -> 3.35 / 1.90 -> 1.29; winder.up
  0.5 2.73 -> 1.42. COSTS: tight.down float frames up (lifted 4.4 -> 6.3 %), fs p2_60 stairs17.crowd.down sunk3 1.44 ->
  2.08 and switchback_tight.down lowest sole -0.1 -> -178.8 mm (one deep sink) -> LM_SPLATMINR (min of smoothed and
  present turn rate) queued as job_k (c15m).
- 06:39 st17t (c12 tour) started. 06:40 orch35 KILLED (was waiting for orch34) and replaced by orch35b.sh (log
  orch35b.log, writes orch35.pid): only st19pl / st19apl (locoplayer) and st19p / st19ap (locostairs 0.5) - c13 does
  not change own-pace or flat walking, the own-pace / tour pairs are left out (saves ~3-4 h). orch36 waits on it.
- AFTER A RESTART: sh locotour/relaunch.sh (sets aside killed logs, clears stale pids, restarts orch34 / orch35b /
  orch36 with new log names; each skips done runs and built exes). An st20 build killed mid-way: orch36 rebuilds.
  Harness jobs (qs/job_h..k) are optional: rerun by hand (bash qs/job_X.sh > qs/job_X.log).
- 06:5x SW stop-float experiments (all on lm_d15 debug): SWUP=1/2 remove it but lose SW's descent gains (s5 crowd.down
  2.6 -> 4.2, blocked.down float max 29 -> 170 mm; spiral.down own 4.03 -> 4.66) -> NO. SWMAX .08/.10/.12 remove it but
  SW then does ~nothing (c13b -> c14m08 fs 0/0, s5 0/0) -> NO. ATB (ball past the nose to hold a fit): fixes the
  PRE-EXISTING up-stop float (stops.up max 164 -> 52 mm, 30 Hz 161 -> 33) but costs elsewhere (c13b -> ab0: interior
  own 7/3, 0.5 7/1 worse/better) and does NOT fix SW's down-stop one -> park. Next: LM_FIXSWST (keep x blendSteady) and
  SEEN alone (c13se) in job_l; if SWST is not clean, c14 = SEEN alone (+ SW later).
- 07:0x c15d's two regressions isolated: switchback_tight.down -178.8 mm = WB2 (fits a too-short run two treads DOWN;
  the foot lands 10.7 cm short and is planted at that fit's height -> 18 cm into the tread above it: trace
  qs/swt3.err run 3); stairs17.crowd.down sunk3 1.44 -> 2.08 = the lateral shift (SPLATMINR does not help; LM_SPLATMIN
  =0.5 or 0.8 removes it and keeps the spiral gains: qs/iso_min*.txt). New switch LM_FIXWBUP (WB2 only for a run not
  below where the foot was to land). job_m: c15e = c14c + WB2 + WBUP + SPLAT 3 dn S0.6 MIN0.5 (eval + kits).
- GENERAL PATTERN behind the stop float, the SW down float and the WB2 sink: a fit far ahead (two treads up / down)
  that the foot lands well short of is still "atFit" (tolerance 0.3 m) and planted at the fit's height. A fit test
  that checks the foot is over the fitted run (heel past its rear edge going down, ball past the nose going up) is the
  real fix; ATB (ascent half) helped the stop float but cost elsewhere -> later round (stops on stairs).
- 07:1x LM_FIXSWST (SW x blendSteady) = clean: removes SW's stop.down float (150 -> 43 mm) and keeps SW's gains
  (c13b -> c14st: fs 14/15, 18/19; s5 2/13, 3/11; interior 7/21, 7/20 = c14w's); it gives back SW's crowd.down float
  improvement (90 -> 170 mm = HEAD's / c13b's value: pre-existing). SEEN alone (c13se) does only switchback.up 0.5
  1.31 -> 0.22 (with SW 0.09). -> c14d = c14c + patches/turnflight3.py (keepCost x A.blendSteady) = blob
  d30f47a5509cb48bb93d20dc6eba88eaf7e71f60 (written). st20 -> c14d (bld20.sh rebuilds; old exe loco_st20_c14c.exe).
  Patches regenerated for c14d (turnflight on c13b, c14 on HEAD). job_n: lm_c14d == c14st? + player speeds.
- 07:2x c15e (debug, on c14c's switches) fixes both c15d regressions (crowd.down 1.44 = c14; switchback_tight.down
  lowest -0.08) and keeps the gains (kit spiral down 0.5 13.3 -> 7.9, pace 13.6 -> 6.4; winder up 2.9 -> 1.55; interior
  tight.down own 24.7 -> 16.6). -> c15f = c14d + curves.py + curves2.py + patches/curves3.py (WB2 only for a run not
  below gFoot; shift only on a bend > 0.5 /m) = animator c97549f7bed61d55968674e39d2d2b6c27be9cc2, character.h
  213be593a6a4fc8613ce3e78f57a669dda221855 (c13's + stairLat[2] + turnRateS; NOT yet -w). qs/clean15f, ctree15f; job_o
  checks it (lm_c15f vs c15fdbg) + kits c14d / c15f + player speeds.
- 07:15 lm_c14d == debug c14st (0 diffs); c14d player speeds: run / sprint exactly c13b's, walk same or better. st20
  REBUILT with c14d (07:14). job_p: anim_test c14d (attree15 -> sl/at_qs14d.log) and c15f (attree16 -> at_qs15f.log).
- 07:3x c15f CHECKED: lm_c15f == debug c15fdbg (0 diffs); anim_test ALL PASSED (sl/at_qs15f.log, attree16). c14d -> c15f:
  fs 6/10, 10/12; s5 1/1, 2/2; interior own 5/18, 0.5 5/27; kit spiral down 0.5 13.3 -> 7.9 % (24/2), pace 13.6 -> 6.4
  (26/0), spiral up pace 6.3 -> 5.2 (15/1), winder up 0.5 2.9 -> 1.55 (slide max 19.1 -> 0.5); player speeds as c14d
  but the sprint up a straight flight's slide max 57.8 -> 8.9 cm (60 Hz), 23.4 -> 9.7 (30 Hz) - WB2 keeps the ball on
  the top landing's edge. anim_test c14d ALL PASSED (at_qs14d.log == c13b's but timings).
- 07:22 st17t DONE (c12 tour): all n 819 slide 0.95/1.71/37.6; straight 572 0.80/1.54/28.7; turning 187 1.21/3.05/24.0;
  standing 60 1.59/1.92/37.6; planted > 15 mm off 71 of 74754 foot-frames; lowest -294.2 mm. st16at (HEAD tour) running.
- 07:4x locoplayer block v3 (locotour/block_player.cpp; v2 = block_player_v2.cpp; spiral part block_spiral.inc):
  LOCO_NPCSPIRAL=n -> a test spiral of collision boxes (kit spiral: rIn 0.1, rOut 0.85, walking line 0.55, 30 deg,
  12 treads of 0.22 m = a full turn, landing over treads 0-1 with >= 1.95 m headroom, outer wall with the way in at
  -60..0 deg and out at 30..60 deg onto a platform at 45 deg) at a 2nd flat site; n walkers up / down 2 min, circle
  steering (aim 0.3 rad on at r 0.55 / cos 0.3 so a follower holds the line), "locoplayer spiral up/down npc" lines +
  walking-line error. LOCO_PLAYERLAPS=0 allowed. st20 app.cpp -> v3 (v2 copy app_st20_v2.cpp, v2 exe
  bin/loco_st20_v2.exe); st21 = st20 + c15f. bld2021.sh builds st20 (v3) then st21 (log bld2021.log). UNTESTED in game.
- 07:5x RUN SPEED: the game runs ~21-33 s real per test second (locostairs 108 s took 39-60 min). Automated runs step
  the sim at a fixed 1/30 s (app.cpp simDt) and animate every step (visibleDist from rig.cam each updatePeds), so
  --renderevery only changes the cost -> run_ap2.sh now uses --renderevery 30 for all new tags (st16* / st17* keep 6)
  and TIMEOUT 10800 (was 4800 = 80 min: a 2-lap locoplayer run at renderevery 6 would have been killed). Old script:
  run_ap2_v1.sh. Compare only runs made with the same setting (all st19* / st20* / st21* are at 30).
- c15f blobs WRITTEN (c97549f7 / 213be593); patches qs/curves_{animator_d30f47a5,character_bd6d781f}.patch (on c14d)
  and qs/c15_{animator_002915d0,character_1628ef2e}.patch (on HEAD) reproduce them. Draft qs/signoff_c15_draft.txt
  (SPIRAL / ELSEWHERE pending: orch37 after orch36: st20sp / st21sp spiral walkers, st21n, st21p).
- 07:28 orch35b + orch36 KILLED (both only waiting) -> ONE chain locotour/orch40.sh (log orch40.log, pid orch40.pid):
  waits for orch34 (st16at), then st20sp (spiral smoke test / c15 before; needs st20's v3 exe, else skipped and
  retried after st20p), st19pl, st19apl, st19p, st19ap, st19n, st20n, st20pl, st20p, [st20sp], st21sp, st21n, st21p.
  relaunch.sh updated (orch34 + orch40 + bld2021 if not done).
- 07:4x spiral walkers made a ONE-WAY CONVOY (the 0.75 m stair is too narrow to pass): all up 6 s apart at one pace,
  wait at the end (or stuck 3 s within 2 m of it), then all down 4 s apart, last-arrived first; flips after all are
  there or 60 s. The first v3 build (two-way traffic) was stopped; block_player_v3a.cpp = that version. st20 / st21
  app.cpp now carry the convoy v3; bld2021.sh restarted (log bld2021.log); st20's exe meanwhile = the v2 one.

## 26. 2026-10-20 08:05 c12 SIGNED OFF (snapshot-safe sent to main)
- st16at DONE 08:00 (HEAD tour). Tour HEAD -> c12: all 0.98/1.74/37.6 -> 0.95/1.71/37.6, scuffs 37 -> 26, turning
  1.35/3.08/29.2 -> 1.21/3.05/24.0, floats 77 -> 71, big slides 5 -> 4. Sign-off text = qs/signoff_c12_draft.txt (sent
  08:05 with blobs 63a9f73e / 9dfb632f, patches on bcb8a71). orch34 done.
- bld2021 done 08:00: st20 (c14d + v3 convoy) and st21 (c15f + v3) built. orch40 started its runs: st20sp 08:00.
- WAITING ON: lead's landing of c12; orch40's runs for c13 (st19pl/apl, st19p/ap), c14 (st19n/st20n, st20pl, st20p),
  c15 (st20sp/st21sp, st21n, st21p). Drafts: qs/signoff_c13_draft.txt (LOCOPLAYER, STAIRS05), qs/signoff_c14_draft.txt
  (NPCSWITCH, PLAYER), qs/signoff_c15_draft.txt (SPIRAL, ELSEWHERE).
- 08:02 SMOKE TEST FOUND: the station is Calle Luna (stair 32.7 m, top z 17.6, foot 3.1) and the old site search
  ("no lane AND no collider within 6 m": collidersNear returns whole grid cells) found NO spot -> "no open flat spot for
  the switchback" (so v1/v2 locoplayer runs would have had NO switchback) and none for the spiral. Killed orch40 and
  its switchback-less st19pl (partial log log_st19pl.nosb_partial.txt; st20sp log -> log_st20sp.nosite.txt).
- 08:07 block v3c (block_player.cpp; v3b = convoy without the fix): lqBlocked (a real collider footprint within 0.4 m
  of a sample point, height overlapping 0.05..4 m) + lqCarNear (9 m) + lqOpenAt (flat to 4 cm over the footprint,
  0.15 m over waiting lines) + lqRings (2 m rings out to 80 m from the stair foot; roads allowed: population off);
  switchback tries lsDir then lsRight; spiral 12 m clear of the switchback. Failure logs now say "within 80 m" (the
  marker orch41 greps in an exe for v3c). ALL FOUR trees (st19, st19a, st20, st21) have the same v3c app.cpp now
  (pre-v3c copies app_<t>_pre_v3c.cpp). bld41.sh builds st20, st21, st19, st19a (marks <t>.building). orch41.sh runs:
  st19p, st19ap (locostairs, block-independent: first), st20sp, st19pl, st19apl, st19n, st20n, st20p, st20pl, st21sp,
  st21n, st21p. relaunch.sh -> orch41 + bld41.
- 08:17 / 08:26 st19p (c13b) / st19ap (HEAD) DONE at renderevery 30 in ~10 min each (was 28-60 min at 6). Same
  numbers as st17p / st16ap (deterministic: the render rate changes nothing). HEAD -> c13: sunk3 up 15.9 -> 2.0, down
  17.9 -> 2.8 (c12 2.4 / 2.8); filled into qs/signoff_c13_draft.txt (STAIRS05 done; LOCOPLAYER pending).
- 08:3x v3c SMOKE TEST (st20sp): site search WORKS (switchback at 1855.9 -638.0 z 3.04 along lsDir; spiral at 1844.1
  -648.0 z 3.37) but the walkers NEVER MOVED: ai.cpp BRAIN_GOTO only walks to a goal more than 0.6 m away (my spiral
  aims were 0.18-0.5 m) -> no spiral lines. Same rule: the switchback walkers' turnaround "< 0.4 m from the end" never
  fires (they stop 0.6 m short) -> stuck at the top after one ascent.
- 08:43 block v3d (block_player.cpp = block_player_v3d.cpp): goals 0.8 m ahead (lines: 0.8 m along the path; the turn:
  a vector field - the circle's tangent turned toward the walking line by 2.5 rad/m - 0.8 m out), "there" within
  0.8 m, spPathNear (nearest point within 1 m ahead: the way round passes over its own start), a state LOG every 15 s
  ("locoplayer spiral t.. walker k: up/down r .. at .. deg, .. m up, s .., v"). All four trees -> v3d. orch41 + bld41
  killed; orch42.sh (log orch42.log): st20p (now, block-free), st19pl, st19apl, st19n, st20n, st20pl, st20sp, st21sp,
  st21n, st21p; bld42.sh (log bld42.log) builds st19, st19a, st20, st21 (marker "locoplayer spiral t%"). relaunch.sh
  -> orch42 + bld42. NOTE the Bash safety check refuses "rm -f $VAR/*" forms: use literal paths or mv.
- 08:53 st20p DONE (c14d at 0.5): up sunk3 2.0 -> 1.2 %, down 2.8 -> 2.7; slides ~same; bigslides 8 -> 8 (in the c14 draft).
- PLAN FROM HERE (08:55): orch42 -> st19pl / st19apl => fill LOCOPLAYER in qs/signoff_c13_draft.txt (python3
  locotour/lpcmp.py log_st19apl.txt log_st19pl.txt) and SEND c13 (snapshot-safe, on c12). Then st19n / st20n /
  st20pl => fill NPCSWITCH / PLAYER in the c14 draft and SEND c14 (on c13). Then st20sp / st21sp / st21n / st21p =>
  fill SPIRAL / ELSEWHERE in the c15 draft and SEND c15 (on c14). Then report to the lead: the stop float (HEAD too) as
  the next stairs item; tight spirals outside the kit spec (validator rejects) -> parked.
- 09:10 TWO MORE TEST-GEOMETRY BUGS (found in st19pl v3d: "switchback up walk, 19.4 s, 3.3 m, 0.17 m/s", deep log
  "switchback up run ... v 0.00" = the player stopped at the first step): physics.cpp capsuleOverlap skips a collider
  only when its top is < 0.35 m above the capsule base, and the 0.3 m capsule reaches two treads on -> two 17.5 cm
  risers (0.35) are a WALL. The metro's 17 cm risers just pass. (Worth telling the lead: the kit spec's spiral risers
  0.19-0.22 m would be unwalkable for peds with box-step colliders.) The v3d spiral (22 cm) had the same problem plus a
  landing over the way in (head height).
- 09:16 block v3e (block_player.cpp = block_player_v3e.cpp): switchback kLqR 0.17 (log "17 cm risers"); spiral 8 treads
  of 17 cm (240 deg), landing 240..300, the way in radial at 350 deg through a 60-deg wall gap, walkers stop by angle
  at 280 deg on the landing (kSpStop; a goal on the circle 0.6 m off is reached 66 deg early), max 2 walkers. Checked
  offline with a python model of capsuleOverlap + ground (no blocking at r 0.45-0.55 or on the way in). bld43.sh
  (st19, st19a, st20, st21) + orch43.sh (st19pl, st19apl, st19n, st20n, st20pl, st20sp, st21sp, st21n, st21p).
  relaunch.sh -> orch43 + bld43.
- 09:3x SENT to main: the 0.35 m step-over rule blocks stairs with 2 risers >= 0.35 and treads < 0.30 (kit spec straight 0.18x0.28 and spirals 0.19-0.22 unwalkable for peds); fix suggestions; not my files.

## 27. 10-20 09:5x-10:1x: st19pl done, the chase test fixed (v3f), bld44 / orch44
- 09:52 st19pl (c13b, v3e) DONE: player straight up walk/run/sprint sunk3 0.0/0.0/3.8%, down 0.0/0.0/2.9; switchback
  up 6.8/4.4/9.8, down 0.7/2.1/9.6; deep 35, bigslide 2; chase3 up only 55 steps 1.5% - NO chase5, NO chase down.
  st19apl (HEAD, v3e) started 09:54:44.
- WHY (v3e chasers): they started 3 / 6 m behind the player's start. Down: the top is 3.4 m from the elevated
  platform's END (lsTopC = st.local(E*(H-3.4), ...)), so 4.5 / 7.5 m back is off the platform -> they spawned on the
  street below and never reached the flight. Up: 10 m past the foot is INSIDE the test switchback (at 1855.9 -638)
  -> chase5 stuck. Also measured only while the player was under way, and BRAIN_GOTO's ai_pace::behind would hold
  the 5 m/s one behind the 3 m/s one (same line, 0.35-1.8 m ahead -> capped to theirs).
- 10:05 block v3f (block_player_v3f.cpp, made by fix_v3f.py from v3e; block_player.cpp is STILL v3e): chasers beside
  the player's start on the other side (0.9 m across), chase5 0.6 m back, chase3 1.2 m (first free of two spots, level
  and no collider within 0.32 m), standing until the player sets off, measured until within 1 m of the other end (the
  segment waits, max 25 s), "locoplayer seg %d chasers: chase3 there/stuck s m m/s, chase5 ..." a segment;
  LOCO_CHASE=1 = straight run/sprint segments only (2 laps = 8 segments). apply_v3f.py put it in st19 + st19a app.cpp
  ONLY (st20 / st21 stay v3e). Exes: st19/bin/loco_st19c.exe (c13b + v3f), st19a/bin/loco_st19ac.exe (HEAD + v3f);
  loco_st19.exe / loco_st19a.exe stay v3e (orch43's st19n uses loco_st19.exe).
- 10:07 bld43 KILLED (PID 28534) during its st20 build; the build (wrapper PID 1027) went on. bld44.sh (log bld44.log):
  waits for it, marks st20 built, syntax-checks v3f (-fsyntax-only, syntax_v3f.log), builds loco_st19c.exe and
  loco_st19ac.exe (<t>c.building markers), then st20/st21 (v3e) if their exes lack v3e. orch44.sh (log orch44.log):
  st19ch then st19ach (LOCO_CHASE=1 locoplayer), each holding orch43 ($T/hold) and waiting for the running game to
  end. relaunch.sh -> bld44 + orch44 + orch43.
- PLAN: st19apl done -> fill LOCOPLAYER (lpcmp.py log_st19apl.txt log_st19pl.txt) and SEND c13 with chase3-up, saying
  the full chase (3 and 5 m/s, up and down) follows; then st19ch / st19ach -> send the chase addendum
  (lpcmp.py log_st19ach.txt log_st19ch.txt). Then c14 (st19n / st20n / st20pl), c15 (st20sp / st21sp / st21n / st21p).
- 10:12 st19apl (HEAD, v3e) DONE. HEAD -> c13 player totals: sunk3 / lifted 5.8 / 3.9% -> 1.1 / 2.5%, deep 175 -> 35;
  run 15.1 / 7.8 -> 0.9 / 6.0; sprint 30.9 / 21.7 -> 5.5 / 10.6 (lptot.py). Worse: switchback down run/sprint lifted
  17 / 20 -> 20 / 31% (small samples), straight up sprint max slide 44 -> 58 (the skim; c15 fixes), chase3 up 0 -> 1.5%.
- 10:2x SENT c13 snapshot-safe to main (qs/signoff_c13_draft.txt as sent): patches checked (on c12 and on HEAD, giving
  24831ff3 / bd6d781f), promised the chase addendum (st19ch / st19ach) in ~1.5 h.
- 10:14 v3f syntax check OK (30 s); bld44 building loco_st19c.exe (10:14), then loco_st19ac.exe, then st21.
- 10:2x st19n / st20n (v3e) DONE but the switchback walkers JAMMED: up 410 steps, down 17 (lanes 0.36 m apart on a
  1.1 m flight, two capsules need 0.6 m; each turned back alone at the top and met the next coming up). c13 -> c14 up:
  sunk3 2.1 -> 1.8, deep 218 -> 187, slide max 60 -> 44.5. Superseded by the v3g runs st19ng / st20ng.
- 10:33 st19ch (c13b + v3f chase) DONE in 4.5 min (the machine is fast now: memfree ~11 GB): chase5 up 64 steps 0.0 /
  0.0%, down 56 steps 0.0 / 4.4%; chase3 down 100 steps 0.0 / 1.5% (avg only 1.98 m/s, 2.7 m extra way: held up at
  the start?); chase3 UP never ran ("no room": its spots on the other side 0.9-1.2 m back at the foot were taken).
- 10:3x block v3g (fix_v3g.py from v3f -> block_player_v3g.cpp; apply_blk2.py v3g replaces whatever block is in a
  tree): the switchback walkers one way at a time (a file at flight 1's foot, up 4 s apart to own spots on the top
  platform (first up furthest in), all there or 60 s -> down, the last up first, back to own spots in the file; along
  the flights' middle; goals 0.6 m past the spots; "locoplayer switchback turn N" lines + state every 15 s; window
  170 s) and the chasers' spots (chase3 on the player's side 1.2 m back, goal 1.5 m short of the player's end; chase5
  the other side 0.6 m back; first free of 5 candidates, logged in lap 1). ALL FOUR trees' app.cpp = v3g now (md5
  e9f64a7d); syntax check OK. Exes: loco_st19.exe / st19a / st20 = v3e, loco_st19c / st19ac = v3f, loco_st19g /
  st19ag / st20g / loco_st21.exe = v3g (bld45).
- 10:37 bld44 KILLED (after st19ac; its st21 compile stopped at once), orch43 KILLED (its st20pl game went on as an
  orphan). bld45.sh (log bld45.log): st19g, st19ag, st20g, st21. orch45.sh (log orch45.log): first ready job of
  st20pl st19chg st19achg st19ng st20ng st20sp st21sp st21ng st21p (st20sp uses loco_st20g.exe so both spiral runs
  are v3g). orch44 still runs st19ach (v3f HEAD chase). relaunch.sh -> bld45 + orch44 + orch45.
- EVIDENCE PLAN: c13 addendum = st19chg vs st19achg (+ st19ch vs st19ach). c14 = st19pl vs st20pl (PLAYER, v3e),
  st19ng vs st20ng (NPCSWITCH, v3g). c15 = st20sp vs st21sp (SPIRAL), st20ng vs st21ng + st20p vs st21p (ELSEWHERE).
- 10:47 st20pl (c14d, v3e) DONE: bit-for-bit st19pl in every case (c14 changes nothing for the player).
- 10:52 / 11:05 st19chg / st19achg (v3g chase) DONE: chasers 2.96 / 4.81 m/s, all 8 segments. HEAD -> c13: chase5 up
  43.4 / 0.6 -> 0.0 / 0.0 (deep 142 -> 0, slide max 37 -> 1.6), down 13.9 / 31.6 -> 0.0 / 5.1; chase3 up 0.9 -> 1.0,
  down 0.0 / 2.1 both. 11:0x SENT the c13 addendum to main.
- 11:01 st19ng (c13b, v3g walkers one way at a time) DONE but the walkers still get HELD UP at the landing's turn
  (15-35 s, both ways: state lines "v 0.00" at z 1.36-1.70): not aiStaticDetour (addCell boxes have flags 2 =
  building, skipped) - likely the turn easing slowing them under 0.15 m/s (ai_turn::ease) -> aiWalkRound's stuck
  step-aside (1.1 m sideways into the walls). Up 444 steps sunk3 3.6% deep 423 (standing toes in risers), down 75.
  The A/B with st20ng is still fair if the motion is the same (the AI doesn't read the animation): check the state
  lines match.
- 11:17 st20ng (c14d, v3g) DONE: = st19ng (motion lines identical; up 444 -> 441 steps, sunk3 3.6 = 3.6, deep 423 =
  423): c14 shows NO change on the test switchback while the AI holds the walkers up at the turn. bld45 done (st21 v3g
  built 11:17). orch45 now: st20sp (11:17), then st21sp, st21ng, st21p.
- 11:18 block v3h (fix_v3h.py from v3g): the walkers' landing turn as a half circle r 0.5 round the inner wall's end
  (centre aL + 0.25, 0.35 m clear of the end wall; lines at x +-0.5) so the goal 0.8 m ahead is <= 46 deg off (the AI's
  ai_turn::follow keeps full pace under 50 deg); state lines now add x / a, detourT, stuckTimer, yaw off. In st19 + st20
  app.cpp only (md5 bb37ea2e; st19a / st21 stay v3g). bld46.sh -> loco_st19h.exe, loco_st20h.exe; orch46.sh -> st19nh,
  st20nh (LOCO_PLAYERLAPS=0 LOCO_NPCSWITCH=4), holding orch45 via $T/hold. relaunch.sh -> + bld46 / orch46.
- AI OBSERVATION for the lead (not my files): BRAIN_GOTO walkers at 0.5 m/s round a switchback landing get held up
  15-35 s (v 0); suspected: ai_turn::follow cuts pace to ~0.1 when the goal swings >100 deg off the body -> under 0.15
  m/s for 0.9 s -> aiWalkRound's step-aside (1.1 m) into the walls. v3h's state lines (detour / held timers) will show.
- 11:21 st20sp (c14d, v3g) DONE but the SPIRAL WALKERS NEVER GOT ON ("0.00 m up" all run): walker 0 stood 45 s at the
  wall's gap (r 0.94, -3 deg), walker 1 went round OUTSIDE the wall to the stop angle. st21sp (v3g) the same (wasted).
- 11:24 block v3i (fix_v3i.py from v3h): ONE spiral walker placed on the walking line at 340 deg (ground, in the wall
  gap, 0.38 m clear of the landing's end), up round to 285 (stop at 280 as before), down back round with the way going
  on past 340 by 1.2 rad (the goal ahead), stop by angle at 330-345 (phi 5.934..6.196); marker "(on the walking line)".
  st20 + st21 app.cpp = v3i (md5 fcca4dac); st19 = v3h (bb37ea2e); st19a = v3g. Syntax checks: synchk/ (a copy of
  st19's src + build/gen) with apply_blk2.py, -fsyntax-only ~26 s.
- 11:26 orch45 + orch46 KILLED (orch45's st21ng went on as an orphan); orch47.sh (log orch47.log): first ready of st21ng
  st19nh st20nh st21p st20spi st21spi. bld46 builds loco_st19h.exe (v3h) then loco_st20h.exe (v3i); bld47 then
  loco_st21i.exe (v3i). relaunch.sh -> bld46 + bld47 + orch47.
- EVIDENCE PLAN NOW: c14 = st19pl vs st20pl (PLAYER: identical), st19nh vs st20nh (NPCSWITCH, v3h half-circle turn),
  st19p vs st20p (locostairs). c15 = st20spi vs st21spi (SPIRAL, v3i), st20ng vs st21ng (switchback, v3g) + st20p vs
  st21p (ELSEWHERE).
- 11:39 / 11:44 st19nh / st20nh (v3h half circle) DONE, motion identical: c13 -> c14 up 449 steps sunk3 1.3 -> 1.5%,
  deep 151 -> 174; down 286 steps 1.3 -> 1.5%, deep 91 -> 103. The extra = TWO footsteps: one stance (0.73 s) planted
  with its heel 17 cm in the tread above while the AI had the body facing back down flight 2 at the turn, one standing
  foot. The state lines (x/a, detour, held, yaw off) showed the first walker STOOD on the landing: it cut 0.2 m inside
  the half circle and its goal "0.8 m on along the way" was only 0.6 m off -> BRAIN_GOTO doesn't move; the rest held up
  behind it (held / detour timers, yaw off up to 178 deg). My test's fault, not the AI's.
- 11:34 st21ng (c15f, v3g) DONE vs st20ng: switchback walkers identical; player lap: straight up sprint slide max 58.1
  -> 3.9 (the skim fixed) but lifted 5.7 -> 13.2% (14 steps); switchback up sprint 0.0 -> 11.5% sunk3, deep 0 -> 3,
  lowest -15 -> -288 mm (a just-planted foot across the inner wall's end at the turn: heel -19, ball +254 then -29 cm).
  Harness (plsum c14d c15f) shows neither -> need st21pl (2 laps) vs st20pl.
- 11:4x block v3j (fix_v3j.py from v3i): the switchback walkers' goal pushed along the way until >= 0.8 m from them;
  marker "goals 0.8 m off". st19 + st20 app.cpp = v3j (md5 45fe58aa). bld48 (after bld47): loco_st19j.exe,
  loco_st20j.exe. orch49.sh reads jobs49.txt (tag|tree|exe|marker|build|autoplay|env|done) each round - add jobs there.
  Queue: st21p (orphan of orch48, running), st19nj, st20nj, st20spi, st21spi, st21pl. relaunch.sh -> bld47/48 + orch49.

## 28. 10-20 12:1x: c14 SENT; c15 in-game picture
- 11:52 / 11:56 st21spi / st20spi (v3i, 1 spiral walker on the walking line): it climbs now (0 -> 1.36 m, on the line
  within 7 cm) but stop-and-go (0.03 m/s average: at the end of each tread its capsule touches the tread two on, 0.34
  above - 1 cm under the 0.35 step-over - and the step-up lag makes it a wall). c14 = c15 there (identical numbers).
- 12:06 st21pl (c15f, v3i, 2 laps) vs st20pl (c14d, v3e): player all 1.1 / 2.5 -> 1.2 / 2.6%, deep 35 -> 39; sprint
  5.5 / 10.6 -> 6.4 / 13.2; straight up sprint slide max 58.1 -> 3.9 (skim fixed) but lifted 5.7 -> 13.2% (+4 frames);
  switchback up sprint +3 deep frames (a foot across the inner wall's end at the turn: h -19 b +254 -> b -29), switchback
  down run +2 (the same wall-end signature c13 had once). Walk a little better (switchback down 3.1 -> 2.0% lifted).
  Harness: WB2 is what fixes the skim (lm_d15 LM_FIXWB=2 LM_FIXWBUP=1: 57.8 -> 8.9; SPLAT does nothing there).
- 12:06 st21p (c15f locostairs 0.5) vs st20p: up identical, down 2.7 / 2.7 -> 2.6 / 2.9%. Neutral.
- 12:10 / 12:14 st19nj / st20nj (v3j: walkers' goals >= 0.8 m off - they flow now, turns done in 39-44 s, motion
  identical): c13 -> c14 up sunk3 1.8 -> 1.2%, deep 81 -> 52, lifted 0.8 -> 0.6; down 0.2 -> 0.1, lifted 5.3 -> 4.9.
- 12:1x SENT c14 snapshot-safe (qs/signoff_c14_draft.txt as sent). orch49 done (all its jobs). No chain running now.
- NEXT: c15 sign-off: harness / kit gains big, in game no gain shown (my spiral is stop-and-go, winders not tested),
  small costs at the switchback turn at speed + straight sprint-up lifted; the skim fix is the visible gain. Decide:
  send as is with the costs listed, or gate WB2 (keeps the skim fix?) - harness says WB2 IS the skim fix.
- 12:19 block v3k (fix_v3k.py from v3j): the spiral walker's line at 0.5 m (kSpLine), 5 cm inside the kit's walking line
  - on 0.55 its 0.3 m capsule touched the outer wall (0.85) and snagged on the wall boxes' corners (suspected cause of
  v3i's stop-and-go). st20 + st21 app.cpp = v3k (md5 f5eba0b4). bld49 -> loco_st20k.exe, loco_st21k.exe; orch49
  (rerun; round 1's log = orch49_round1.txt) with jobs49.txt = st20spk, st21spk. relaunch.sh knows bld49.
- 12:33 / 12:43 st20spk / st21spk (v3k, the spiral walker at r 0.5) DONE: it FLOWS now (up in 30 s, down, up again:
  the wall touch was the stop-and-go cause). c14 -> c15: up 1.0 -> 1.1% inside (deep 15 -> 17), down 0.0 both (c14 is
  already clean on this 17 cm spiral). Motion identical.
- 12:4x SENT c15 snapshot-safe (qs/signoff_c15_draft.txt as sent), with "In short: ... Yours to land now or hold."
- ALL THREE (c13 + chase addendum, c14, c15) SENT. No chain running. Exes kept: st19 tree loco_st19{,c,g,h,j}.exe,
  st19a loco_st19a{,c,g}.exe, st20 loco_st20{,g,h,j,k}.exe, st21 loco_st21{,i,k}.exe.
- NEXT CANDIDATES (for the lead to pick): (1) the stop float (HEAD too: a walker stopping mid-flight going up stands a
  foot 15-16 cm in the air; idea: atFit must check the foot is over the fitted run - ball past the nose going up, heel
  past the rear edge going down; ATB prototype fixed the up case but cost elsewhere); (2) in-game floats running /
  sprinting DOWN (switchback 20-31% lifted, straight sprint 11%; the harness shows far less: the body/capsule model
  differs?); (3) feet across a wall's end at a switchback turn at speed (c13: 1, c15: 3 per 2 laps); (4) tight spirals.
- 12:5x SENT to main: all three sent; propose parking tight spirals (kit spec keeps walking lines >= 0.5-0.55 m);
  next candidates 1) stop float, 2) in-game floats running/sprinting down, 3) feet across a wall's end at a switchback
  turn at speed; KIT SPEC CORRECTION: a 1.7 m spiral's outer wall (0.85) touches the ped capsule on the 0.55 m walking
  line (snags: 6% moving) -> wall >= line + 0.35 m (about 1.8 m across) or peds at 0.5 m; 17 cm riser limit stands.
  Nothing running. Waiting for the lead's pick.

## 29. 10-20 13:35 FOURTH RESTART (~13:31): nothing of mine was killed
- All chains had finished by 12:43 (orch49 done, bld49 done, no .building markers); every evidence log holds its done
  line (st16ap .. st21spk); all candidate blobs are in the object store (c12 63a9f73e/9dfb632f, c13 24831ff3/bd6d781f,
  c14 d30f47a5, c15 c97549f7/213be593); HEAD bcb8a71 with HEAD's anim blobs 002915d0 / 1628ef2e -> bases unchanged.
- The coordinator's restart note asked to rerun the slow-pace sign-off and the jog/run lead + switchback fix: both were
  sent long before (c12 08:05; c13 10:2x + chase addendum 11:0x), and c14 (12:1x) and c15 (12:4x) too. Told the lead so
  (hand-back), with the paths of the exact texts in case a message was lost. Waiting for the next item.

## 30. 10-20 13:5x-16:1x OPTION 3: feet in the switchback's walls at the turn at speed (on c14 d30f47a5 / bd6d781f)
- Lead (snapshot 61 = c12+c13+c14, HEAD now c7a63ae with animator d30f47a5 / character.h bd6d781f): c15 held; do option
  3 on c14, then re-offer c15 on top "no worse at the turn"; then option 2 (floats running/sprinting down), option 1
  (stop float). Spirals parked. Told the lead bin/nt_ai102c.exe not needed.
- In game (v3l block = fix_v3l.py: LOCO_SWONLY=1 traces, st20 tree, loco_st20l.exe, log_st20sw.txt, trcls.py): 34
  traces, 11 OUTER wall (not the inner wall's end): the run (4.1, slowed to ~2 m/s at the turn) presses the capsule
  against the second flight's outer wall (root |x| 0.79 = 1.1 - 0.3); the outer foot's landing, predicted ahead of the
  body, lands in it (whole foot, or toe tip); its probe reads -0.45 (street) and the heel sinks ~29 cm. Swing probes
  ahead read -0.45 from 0.5-1.0 m out for the whole swing. The other 23 traces are tread floats/sinks (option 2 land).
- Harness: locosim.h walls (LM_SBWALLS; backups *_pre_walls.*), wall depth metric (planted_in_wall_3cm_frames,
  planted_in_wall_max_cm; backup locometer_pre_depth.cpp), LM_SBGAME = the game's route (square corners at the
  landing's middle, aim 1.1 m) + the player's turn (15 / 10.5 rad/s, x0.45 pace facing >1.2 rad off) - reproduces the
  game: c14 4.1 up 177 in-wall frames (max 16 cm, deep 41), down 193. Scripts: sbw.sh <bin> [g], sbwcmp.py [-g] bins,
  ievdiff.py, ievreg.py, trw.py (trace view; harness trace now prints rp = root pos), mkdbg.sh <name> (debug tree
  ctree16g with WGSCAN / WGNONE / WGSW prints for the traced run; patches/dbgify.py).
- Patch patches/wallguard.py (versions kept as wallguard_c16d/e/f/g/h.py): character.h fields wallPull, wallSeen,
  wallScan (additive). Scan: no ground = a sample <= -0.9 nearer than the steepest stair allows (-g > 0.8|q| + 0.1), or
  past a 40 cm edge with its low side < -0.42 (both sides), all within 1 m (c16i); the fit keeps toes / heel off it
  (shift up to 0.45). Where it was to land is no ground -> wallScan -> probes <= -0.42 there are "none" too (beyond
  0.5 m) -> pull the landing 0.3 of the way to the predicted body position (at) per detection, max 0.6 m, easing x0.85
  per ground reading (c16h). Lessons: c16d's backward edge marked legit lower treads (other flight higher) -> fixed by
  the low-side rule; level rule without distance -> sprint-down false positives (c16e); pulling to the root now ->
  foot left behind / dragged (c16f); a kept pull over-corrects when the body is stopped by the wall (decay, c16h);
  sprint scans 1.2+ m ahead see the end wall the body never reaches (c16i: cliffs only within 1 m).
- c16h game route: 4.1 up/down in-wall 177/193 -> 0/0, deep 41/49 -> 0; landing deep sinks (>20 cm) 94 -> 0; cost:
  landing lifts 1.5-10 cm up (+18 frames > 5 cm at 4.1), more at 6.9 (sprint had no wall frames in c14 here).
- 16:1x-17:0x more versions (patches/wallguard_c16<x>.py kept): c16j = pulls only from fresh findings (the probe's or
  the scan's landing still within 10 cm of where it was found: a body stopped by the wall brings a landing predicted past
  it back by itself); c16l = the near-probe "none" rule 0.5 -> 0.3 m (tight spirals and winders' inner sides drop 0.45
  within 0.5 m; steep straight stairs too); c16m = no pull decay once a tread is fitted (the decay after the last fit
  moved the landing off it: kit L stair w0.8 down heel in a riser); c16n = decay early only; c16o = no decay at all:
  every other landing probe of a pulled swing goes where it would land unpulled (wallCheck / wallFlip fields) and the
  pull halves once that finds ground (fixes the L stair up floats c16m had). c16k (scan pull only when no fit) was
  worse - the AI-route end wall needs shift + pull.
- In game st22sw (c16j, st22 tree = st20 + c16j, loco_st22a.exe) vs st20sw (c14): switchback down run sk3 8.0 -> 6.4%,
  lifted 17.3 -> 15.6, deep 16 -> 10, lowest -29 -> -17 cm, max slide 8.6 -> 0.9; up run sk3 4.4 -> 2.1, lifted 12.1 ->
  4.0; up sprint sk3 12.3 -> 4.9, deep 25 -> 10; down sprint identical. Outer-wall traces 11 -> 8, and none sinks now
  (heel on the landing at +0.1/+0.2; toe tip 0-9 cm past the wall face).
- c16o harness vs c14 (lm_c14w = c14 with today's harness): walls game route 4.1 up/down 177/193 -> 0/0, deep 41/49 ->
  0; AI route 4.1 up 78 -> 12 frames <= 0.5 cm; no-wall suites / player speeds identical but switchback 1.55 (better);
  changes only where the harness has open drops (spirals' edges, L stair sides, between switchback flights): spiral
  down better (kit iv5 sk3 13.3 -> 11.7, floats 1428 -> 1031), spiral up max slide 42.9 -> 56.8 (open-edge artifact).
- c17b = c15 + the c16o guard (patches/mkc15guard.py -> wallguard_c15.py; c15 from blob c97549f7 / 213be593):
  harness tree qs/ctree17 (ctree15f + wall fields). c17a (c15 + c16m guard) already fixed the walls like c16m.
- Game runs queued (orch49, jobs49.txt): st22msw (c16m, running), st22osw (c16o), then st22opl/st20pl3 (locoplayer
  2 laps), st22onj/st20nj3 (4 walkers), st22op/st20p3 (locostairs 0.5) - c14 baselines rerun on loco_st20l.exe (v3l).

## 31. 10-20 17:4x PARKED (lead: usage almost gone) - killed by PID: orch49 (21242), the st22osw game (run_ap2 10721 ..
   loco_st22o.exe 10757 + its wine helpers), my wait loops. Nothing of mine is running.
- OPTION 3 STANDS AT: candidate c16o = c14 + patches/wallguard.py (copy wallguard_c16o.py); files SP/loco/c16o/; blobs
  written: animator 47845aa1af63f19a03df0844932262c26b30c5bf (base d30f47a5), character.h d2d5127d2375ae87f343876206b42ce8e0c29b15
  (base bd6d781f, +5 additive fields); patches SP/loco/qs/wallguard_{animator_d30f47a5,character_bd6d781f}.patch verified.
  HEAD 7b9146d still has d30f47a5 / bd6d781f. Harness all done (anim_test ALL PASSED at_qs16o.log). Sign-off draft
  SP/loco/qs/signoff_c16_draft.txt - fill [[SW]] [[OW]] [[PL]] [[NJ]] [[P]] [[KL1]] from game runs, then send.
- GAME RUNS LEFT for c16o (st22o tree, loco_st22o.exe built): st22osw (was killed mid-run; rerun), st22opl, st22onj, st22op;
  c14 baselines on loco_st20l.exe: st20pl3 DONE (log_st20pl3.txt), st20nj3, st20p3. jobs49.txt holds them all (+ st23sw);
  restart: cd SP/locotour && nohup sh orch49.sh > orch49.txt & (it skips jobs whose logs hold their done line).
  Earlier versions in game vs st20sw (c14): c16j st22sw and c16m st22msw both: up run lifted 12 -> 3-4%, up sprint deep
  25 -> 5-10, down run deep 16 -> 10, lowest -29 -> -17 cm, outer-wall traces 11 -> 8 (heel on the landing now; toe tip
  0-9 cm past the wall face - the probe sits along the move, not the foot, in a turn: possible follow-up).
- C15 RE-OFFER: c17b = c15 (blobs c97549f7 / 213be593) + the same guard via patches/mkc15guard.py -> wallguard_c15.py;
  files SP/loco/c17b/; harness lm_c17b: at the turn identical to c16o (walls 177/193 -> 0/0); game exe built
  SP/locotour/st23/bin/loco_st23b.exe, job st23sw queued (compare with st22osw). Then send c15 on top of c16.
- NEXT after that: option 2 (in-game floats running/sprinting down: e.g. st22sw trace 9, a quick step after the turn
  then landings 29-49 cm high at 30 Hz), then option 1 (stop float).

## 32. 10-26 15:2x RESUMED (lead: usage reset). HEAD bdf3016 (PR #56), anim bases still d30f47a5 / bd6d781f.
- orch49 relaunched 15:20 (old log orch49_round4_killed.txt): st22osw first, then st22opl, st22onj, st20nj3, st22op,
  st20p3, st23sw (st20pl3 already done 10-20). Plan: fill qs/signoff_c16_draft.txt and send; then c17b re-offer with
  st23sw; then option 2, option 1.
- 15:37 st22osw DONE (c16o in game) vs st20sw: run up sk3 4.4 -> 2.1, lifted 12.1 -> 3.4, scuffs 40 -> 2 (steps 44 -> 55);
  run down sk3 8.0 -> 6.4, deep 16 -> 10, lowest -29 -> -17, slide max 8.6 -> 0.9; sprint up sk3 12.3 -> 2.4, deep 25 ->
  5, scuffs 24 -> 16; sprint down identical; outer-wall traces 11 -> 9 (heel on the landing, toe tip 0-7 cm past the
  face). Filled into qs/signoff_c16_draft.txt. Other runs wait for memory (others' builds; gate 3300).
- OPTION 2 lead (harness, ctree18 = c16o + env experiments, lm_c18x; o2.sh / o2cmp.py): the running-down "young"
  floats = touchdown with no tread fit takes Max(near probe's ground, landing probe's) and the near probe lags up the
  flight -> lands 5-30 cm high, settles over frames. X_TDGA=2 (going down, rootVz < -0.3: the landing probe's ground,
  at most 0.2 below the near one): switchback down 4.1 30 Hz lifted 10.2 -> 6.6%, floats 115 -> 75, max 122 -> 77 mm;
  6.9: 18.4 -> 15.2; straight 4.1: 2.2 -> 1.1; cost sk3 6.9 5.4 -> 7.1. (X_LATEA: late swing target -> no effect.)

## 33. 10-26 16:1x-16:5x c16 SIGNED OFF; lead: c16o ACCEPTED for snapshot 64 (main 61cf548, bases unchanged)
- Sent qs/signoff_c16_draft.txt as is (snapshot-safe). Lead: accepted for 64; run the addendum runs (st22opl, st22onj /
  st20nj3, st22op / st20p3) as memory allows - their numbers go into 64's message if in time; ANY WORSE -> tell the lead
  FIRST. Then c17b (st23sw queued after them in jobs49). orch49 running (st22opl started 16:16, game up ~16:27).
- OPTION 2 (running / sprinting down floats), harness findings (lm_c18y = ctree18 + X_FLAT / X_FIRST env; LM_TRACEALL=1
  new in locometer.cpp: the traced run's frames off the stairs too; scripts o2site.py (floats by site / age), o2sink.py
  (sinks: heel at the nose 'edge' vs in the step), trtab.py (trace table)):
  * The landing / floor floats (the biggest share): a swing's ground scan that finds no step (all landing, or the floor
    at the foot) set stairLand -9, so the touchdown took Max(near probe, landing probe) - both lag a run's swing coming
    down the flight (near probe: every other update, 0.66 ease at 30 Hz) -> 5-34 cm high, settling over frames.
    X_FLAT=1 (stairLand = the scan's ground there): sb 4.1 30 lifted 10.2 -> 4.5 %, young floats 67 -> 4; 6.9 30 18.4 ->
    15.1; kit straight 6.9 30 7.6 -> 2.5; full suites 1/24 and 4/16 worse/better, s5 0/6 0/11; BUT spiral.up 30 Hz sk3
    0.17 -> 0.43 (a touchdown 20 cm into the top landing: the flat fit not atFit -> the else branch skipped the Max).
  * -> c18 = c16o + patches/flatfit2.py: a separate field flatLand[2] (character.h, additive) set by such a scan; used
    only at the gait touchdown when stairLand < -8 and atFit; else the old Max. Tread fits / none rule unchanged. Files
    loco/c18/, harness tree qs/ctree19 (lm_c18), game tree locotour/st24 (loco_st24.exe), bld50.sh builds both; job
    st24sw appended to jobs49.
  * First step onto a flight going down from a standing start at a sprint: the swing is past 0.6 when its landing probe
    first sees the drop (the gait's duty shrinks as it speeds up) -> no scan, no fit. X_FIRST=0.9 (first scan up to 0.9)
    fixes those floats (B young 30 -> 2) but adds sinks at a sprint (sk3 5.4 -> 8.9): the late fit's spot is overrun
    (accelerating) and the planted foot's near probe at mid reads the lower tread -> heel 17 cm in the step: the in-game
    'deep h -16.9 b 0 t 0' pattern (heel within cm of the nose). Parked for now.

## 34. 10-26 17:0x-18:0x c16 ADDENDUM SENT (3 parts); c16o IS IN HEAD 5804e7d (snapshot 64); c18c = option 2 part 1 ready
- Addendum: st22opl vs st20pl3 nothing worse (switchback run/sprint better); st22onj == st20nj3 exactly; st22op vs st20p3
  (c14 rerun == 10-20 run exactly): up a little better, down sunk3 2.7 -> 2.6 but lifted 2.7 -> 2.9 (+16 of 7860; told).
- HEAD now 5804e7d: animator 47845aa1 (c16o), character.h d2d5127d. attree64 = git archive HEAD (qs/at64.sh).
- c18 variants (patches/flatfit2.py, flatLand field): c18 = flat ground used at the gait touchdown when atFit -> spiral.up
  30 Hz 20 cm sink (a flat scan along the last tread, the foot landed on the top landing); c18b = also rootVz < -0.3 ->
  loses the run's landing gains; c18c = flatLand < liftGround - 0.04 (a step DOWN onto it) = FINAL: o2b young floats
  354 -> 154, big 193 -> 89, sinks unchanged; full suites 0/9, 0/4 worse/better; s5 0/2, 0/1; interior / kit same or
  better; walls same; up identical; anim_test ALL PASSED on HEAD 5804e7d (sl/at_qs18c.log).
  Blobs written: c18 (on HEAD) animator a106b46371965e934b5b4854193c34f3b8594631, character.h 9e3388ffe21ad688ced96e9d475133146fba4003;
  c19 (= c17b + the same, if c17b lands first) 6d21bed964c15d17dcc9fc93e2c49dbac86cca14 / df304dc63f8103a80444dbf888362b7e903c04ba;
  patches qs/flat_{animator_47845aa1,character_d2d5127d,animator_bf6fa558,character_d7a78f56}.patch (verified).
  Draft qs/signoff_c18_draft.txt ([[GAME]] [[AT]] [[SW]] to fill). Game: st24 tree rebuilt with c18c 18:00 (old exe kept
  as loco_st24_v1.exe = the first c18); job st24sw queued after st23sw. c19 harness: qs/ctree20 -> lm_c19 (queued build).
- Parked experiments (ctree18, lm_c18w): X_FIRST=0.9 (late first scan; more heel-in-step sinks at a sprint), X_HEELP=0.25
  (planted probe nearer the heel going down; trades sinks for floats). Next for option 2: the first step down from a
  standstill (B) and the toe-off over a nose (C) - idea: keep the swing scan's samples and re-fit at touchdown from them.

## 35. 10-26 18:1x c15 / c17b PARKED (lead agreed); c18 -> snapshot 65 on 5804e7d
- st23sw (c17b in game) vs st22osw: at the turn the same (9 outer-wall toe-tip traces, same numbers), BUT sprinting UP
  a new top-step sink in laps 1-3 (traces 1/5/8, segs 2/6/10): the foot lands on the last tread (j=8, 2.89) at the
  height of the tread below (2.70), heel -19, then ball/toe -28.8 / -17 cm (the block's '+254' ball = measured from
  inside the tread box). Up sprint sk3 2.4 -> 6.8 %, deep 5 -> 14, lowest -12.1 -> -28.8 cm. Suspected: c15's short-tread
  (WB2) fit at the last tread before the platform at a sprint picks a landing the foot doesn't reach -> !atFit ->
  lagging probe. Harness (game route, 30 / 60 Hz) does not show it (its sprint up is the same with / without c15).
  PARKED by the lead ("park c15/c17b with a note"); c19 (= c17b + flat, blobs written, lm_c19) parked with it.
- Lead: c16o fully verified (addenda in 64's record; PR #57 waits on other agents' fixes). c18's sign-off -> snapshot 65,
  based on 5804e7d.

## 36. 10-26 18:3x c18 (c18c) SIGNED OFF -> snapshot 65 (HEAD 2c0222e, anim files as 5804e7d)
- In game st24sw vs st22osw: sprint down lifted 26.9 -> 12.5 %, run down 15.6 -> 11.5 %, everything else identical
  (same 32 traces). Sent qs/signoff_c18_draft.txt (snapshot-safe). Addendum runs queued in jobs49 (orch49 restarted
  18:23, old log orch49_round5.txt): st24pl (vs st22opl), st24nj (vs st22onj), st24p (vs st22op).
- Remaining in-game down cases (option 2 part 2): (a) straddle then drop: a foot landing across a nose, heel a few cm
  onto the upper tread (fit overrun, or no fit at the first step from a standstill), put on the upper tread, then the
  planted foot's near probe at mid reads the lower tread -> heel 17 cm in the step (in game trace 19: run, flight 1,
  heel 7 cm before the nose; sprint first step trace 22). (b) run: quick step at the turn then a 3-frame swing landing
  29 cm high (trace 20). (c) toe-off over a nose. Idea: keep the swing scan's samples per foot and pick the touchdown
  height from where the heel / ball actually are (stay on the upper tread when the heel is >= ~6 cm onto it, probe under
  the heel then; else the lower one).
- 18:3x Lead: c18 ACCEPTED for snapshot 65. Send its addendum runs as they finish (as for c16). Then option 2's next
  parts, EACH SIGNED OFF SEPARATELY: (1) the nose landings (straddle then drop), (2) the short swing after a quick step
  at the turn, (3) the toe-off over a nose. Base = c18 (animator a106b463 / character.h 9e3388ff).

## 37. 10-26 19:0x-20:3x c18 ADDENDUM SENT (pl better, nj / p identical); c20 = option 2 part 2 (nose landings + roll-off)
- Experiments in qs/ctree21 (+g debug: TDR / WGSCAN prints), lm_c20x / lm_c20xg, env switches: X_TDR (touchdown pick
  from the swing scan's samples kept per foot), X_TDRN (nose min, 0.03), X_TDRC (curvature limit /m, 0.5), X_TDRT (abs
  turn limit, 99 = off), X_TDRK (nose offset in samples, 0.65), X_TDRL (liftGround check), X_TDRH (pick on the early
  touchdown too), X_NOSEP (roll over the nose: wBall capped at noseD / L, the pin lets the forefoot dip),
  X_NOSEDIP (dip cap, 0.05), X_FIRST / X_FIRSTD / X_FIRSTC (late first scan; down only; curvature).
  Findings: pick alone trades sinks for toe-off floats -> needs the roll (X_NOSEP) whose forefoot must be let below
  the tread plane (pin low = min(heel, pivot)); K 0.8 -> more toe-off floats, 0.5 -> sole dips; 0.65 middle. Edges > 1
  (tight spiral) -> 40 cm sinks: one-edge rule. Abs turn limit cost sprint gains -> curvature limit. The early (held)
  touchdown path needs the pick too (walkers' route sinks). The dip cap 5 cm fixes stairs17.stops.down's 14 cm slide.
  X_FIRST (late first scan): big sprint gain in the game route (first step from a standstill) but walkers' route 4.1
  down sk3 1.8 -> 3.8 -> LEFT OUT of c20 (separate part later).
- c20 = c18 + patches/nosepick.py (fields landG / landScanFrom / landScanDir / landScanHave / heelProbe / noseAt in
  character.h, additive). Files loco/c20/, harness qs/ctree22 -> lm_c20 (== lm_c20x with the d5 env: verified same).
  Harness vs c18c: o2b sinks 478 -> 325, toe-off floats 264 -> 194; walls / players / interior / kit same or better;
  full suites 5/15, 6/16 worse/better: the worse = lowest sole 3-12 mm (the roll's nose estimate, 5 cm samples) and
  spiral_tight.down 60 Hz float frames 51 -> 64 (sinks there 21.3 -> 21.0 %).
- Game: st25 tree (= st24 + c20 files), bld52.sh (build + anim_test qs20, waiting for memory 20:3x), job st25sw queued
  (orch49 restarted 20:18, old log orch49_round6.txt).
- Left for later parts: the first step down from a standstill (X_FIRST, with its walkers' route sinks fixed), the short
  swing after a quick step at the turn (no scan covers it; the foot lands 11 cm off the last scan's line).

## 38. 10-26 21:4x-22:3x c20 IN GAME + DIAGNOSIS; c20b / c20c (not sent yet)
- In game c18 st24sw -> c20 st25sw (8 laps): sprint down sk3 14.9 -> 4.3% (the laps 4-8 mid-flight sinks gone), lifted
  12.5 -> 17.3%; run down identical (its drop: trace 19 of log_st25dsw4); up identical. c21 st26sw == c20.
- The in-game "lifted" is 3 points only (heel, ball, toe): a foot rolling over a nose touches it between heel and ball
  but counts as lifted. The harness uses soleClear (9 points per part). -> locotour/fix_v3n.py (v3n: the same line
  measure added to the in-game summary lines, "sole line: ..."; lpcmp2.py reads it). Needs c18 and c20x rebuilt with it.
- Sprint-down deep frames left in game = the first step down after the turn-around at the top (laps 1-3): heel 1 cm
  before the nose (a=0.55, nose 0.56) -> mode 0 by design (edge). Also the pick refused there (the swing began going
  up: gh > liftGround + 0.04) - no matter, mode 0 anyway.
- Run-down drop (laps 4-8, flight 1, 2nd step after the landing turn): touchdown on the fit's tread (heel 7 cm before
  the nose), pick NOT applied (hp0), probe at the foot's middle (1.5 cm past the nose) reads the tread below -> drops
  (kg then held). Why the pick refused: unknown -> st27d diag (c20b + v3m + v3o: Animator::dbgPick[2][12] why/u/v/cos/
  edges/dn..., patches/pickdbg_game.py), job st27dsw4.
- c20b (patches/heelat.py): mode-1 foot probed AT the heel (was lerp 0.2 toward the ball: at the nose for d 3-4 cm).
  Harness o2b sinks 325 -> 287, toe-off floats 194 -> 200; full / interior / kit / players ~same.
- c20c (patches/noseref.py, on c20b; fields noseLo/noseHi/noseTest): a mode-1 planted foot's probe goes every other
  update to the middle of where the nose may be (bisection to 1.2 cm); the roll pivots there. o2b toe-off floats
  200 -> 164, lifted lower in most sets, sinks 287 -> 289. fullpic c20c running.
- Short swing after a quick step (part 3): trace 21 of log_st25dsw4 - a quick step from the landing carries the foot
  1 m down the flight (st 0 -> 0.88 over 6 frames, over the landing's height), then a 3-frame gait swing with
  groundAhead stale at the landing's height -> planted 29 cm up.
- Harness debug tree qs/ctree24g (lm_c20bg): PICK / PICKTD / PLF prints (patches/pickdbg.py after dbgify.py).

## 39. 10-26 22:4x-23:2x c20c CHOSEN for part 2 (not sent yet); lead: main = 41c3ca0 (snapshot 66), anim bases unchanged
- In-game diag st27dsw4 (pick why per touchdown): the run-down drop = why 3 (|v| 10.4 cm > 8: the swing's scan ran 10 cm
  across where the foot came down at the turn exit; its dn 18.5 cm vs the real 7). The reversal step: why 0, mode 0
  (one tread by the samples; the heel really 1 cm before the nose) - an edge, by design.
- X_NS search (qs/ctree26, patches/nosesearch.py: a foot with no pick finds the nose with its own probes; + groundRaw
  kept across tests): sb sinks 289 -> 271 but stairs17.blocked/stops.down float max 2 -> 17 cm and kit straight down
  0.5 m/s floats 75 -> 381 -> DROPPED (known limit in the sign-off).
- c20c = c20 + heelat.py + noseref.py. Clean files loco/c20c/, blobs written: animator fbf469f6f0abd09018dc68de74c81970ea415cae,
  character.h 5bdefd0e4cd8ccdd1ddf5d633922243ae043bc18; patches qs/nose2_animator_a106b463.patch (+105/-3),
  qs/nose2_character_9e3388ff.patch (+7), verified. anim_test sl/at_qs20c.log ALL PASSED, same as c18's.
- Harness c18c -> c20c: o2b sinks 478 -> 289, toe-off floats 264 -> 164, sb heel-in-step 199 -> 118; full suites
  5/19, 6/16, 2/9, 0/12 (worse = lowest sole -3..-8 mm, spiral_tight.down 60 Hz floats 51 -> 64).
- In game: st24nsw (c18 + v3n) done: 3-point numbers = st24sw exactly; sole line lifted run down 8.2%, sprint down
  8.7%. st28 (c20c + v3n) built 23:21, job st28sw running (orch49 restarted 23:12).

## 40. 10-26 23:4x c20c SIGNED OFF (option 2 part 2: nose landings + toe-off over a nose) on HEAD 41c3ca0
- Sent: qs/signoff_c20c_draft.txt (blobs fbf469f6 / 5bdefd0e on a106b463 / 9e3388ff). In game c18 st24nsw -> c20c st28sw:
  sprint down sk3 14.9 -> 4.3%, sole-line lifted 8.7 -> 1.0% (3-point 12.5 -> 17.3%), run down sole-line lifted
  8.2 -> 3.8% (3-point identical); up identical; traces 32 -> 27.
- Addenda queued in jobs49.txt (orch49 restarted 23:37): st28pl vs st24npl, st28nj vs st24nnj (all v3n), st28p vs
  st24p (locostairs: no sole line there). Send as they finish; flag anything worse first.
- Next (option 2 part 3): the short swing after a quick step at the turn (trace 21 of log_st25dsw4; a quick step from
  the landing travels 1 m down the flight, then a 3-frame swing with groundAhead stale at the landing's height), then
  maybe the turn-exit nose landing (pick why 3) and the first step from a standstill (c21 late scan, X_FIRST).
- 23:5x lead: c20c ACCEPTED for snapshot 67 (fbf469f6 / 5bdefd0e checked on 41c3ca0). Send the addendum (pl, nj, p)
  when in; if worse, say so and whether it changes the sign-off. Then part 3: the first step down from a standstill
  at the top at a sprint + the short swing after a quick step at the turn, same evidence. Keep the blobs unless the
  addendum finds a problem.

## 41. 10-26 23:4x-10-27 00:0x part 3 work (on c20c) + addendum in progress
- c22a = c20c + patches/latescan2.py (c21's late first scan ported; tree qs/ctree27, lm_c22a, clean files loco/c22a/):
  o2b first steps only (t < 2.2 s) better: lifted 15->9, 35->22, 81->46, 81->70; sunk 21->11, 33->9, 24->19, 66->24;
  totals young 149->106, big 69->34, sunk 289->208; full suites / interior / kit / players / walls: same or better.
- In game the test's down segments follow its up segments (stairs still "seen"), so the first step down from the top
  is scanned already -> fix_v3p.py: LOCO_FRESHTOP=1 sets the player's animator stairSeen = 99 at each down segment's
  start (as if from the street). Trees st28f (c20c + v3n + v3p), st29 (c22a + v3n + v3p); bld57.sh; jobs st28fsw /
  st29sw (LOCO_SWONLY=1 LOCO_PLAYERLAPS=8 LOCO_FRESHTOP=1).
- Quick step at the turn (trace 21): c22b (patches/qsrelease.py: let go late in the stance in every case) = wide side
  effects (walls 0 -> 15 frames, spirals' slides) -> dropped; c22c (qsrelease2.py: only in the "both feet off,
  correction > 0.4 m -> quick step" branch, stance late) building/testing (lm_c22cx, fullpic c22c).
- Addendum: st28pl vs st24pl (3-point): straight down sprint sk3 2.9 -> 3.9%, deep 1 -> 3 = the heel < 3 cm rule
  (lands on the tread below at once: 3 frames heel-in-riser instead of 1 after a drop) - the sign-off's known limit;
  switchback down sprint sk3 9.6 -> 5.8%; the rest identical. st24npl (line) next, then nj, p.

## 42. 10-27 00:0x-00:3x part 3 candidates (base c20c fbf469f6 / 5bdefd0e = snapshot 67)
- c22c (release in the both-off branch) DROPPED: player speeds straight 4.1 down slide max 14.9 -> 32.1 cm.
- c22d = c22a + patches/edgeshift2.py (clean; harness tree qs/ctree30 has an X_EDGE switch): the pick's mode 0 (heel
  < 3 cm onto the tread above, or a nose just behind the heel's sample) moves the plant on along the scan past the
  first sample of the tread below (+0.5 cm, max 6 cm), plantSlack. o2b c20c -> c22d: sunk 289 -> 35 (edge 147 -> 4),
  young 149 -> 106, big 69 -> 34; full suites worse = small slides on stairs17 down (blocked step max 0.26 -> 2.6 cm
  at 0.5 m/s 60 Hz); interior / kit same; players / walls down sinks lower. anim_test sl/at_qs22d.log ALL PASSED.
- c22e = c22d + patches/qsga.py: a quick step's touchdown sets groundAhead to the foot's height (the 3-frame swing
  after it had the landing's height). Harness ~same as c22d (spiral.up slide total 14.7 -> 18.0 cm, 30 Hz).
- Clean files loco/c22a, c22d, c22e. Game: st30 (c22e + v3n + v3p) build queued (bld58 via wait58.sh), jobs
  st30sw / st30fsw after the addenda and st28fsw (c20c fresh-top baseline). st29 build cancelled (not needed).
- Addendum pl (c18 st24npl -> c20c st28pl, sole line): straight down sprint sk3 2.9 -> 3.9%, deep 1 -> 3 (the heel
  < 3 cm known limit; lifted 4.9 -> 2.0%); switchback down sprint sk3 9.6 -> 5.8%, lifted 19.2 -> 7.7%; run down
  lifted 1.7 -> 0.0 (straight), 4.0 -> 0.0 (switchback); up identical. nj / p running.

## 43. 10-27 00:45 CONTAINER RESTART (every process died; disk kept)
- Lost: st28nj (mid-run), and st24nnj / st28p / st28fsw / st30sw / st30fsw not started. Done before it: st28pl,
  st24npl (the pl addendum is complete). attree64 was left with c22e's files by a killed at64.sh -> restored from
  attree64_*.orig (5804e7d's 47845aa1 / d2d5127d). New qs/atcopy.sh runs anim_test on a copy (attree64 untouched).
- 00:51 orch49 restarted (old log orch49_round11_killed0045.txt); waits for the lead's flag (snapshot 67 smoke).
  anim_test c22e queued (qs/atcopy.sh qs22e). Builds st28f / st30 survived (exes on disk).
- 01:3x ADDENDUM SENT for c20c: pl (straight sprint down sk3 2.9 -> 3.9%, deep 1 -> 3 = heel < 3 cm limit; rest
  better / same), nj (3-point identical; sole-line down lifted 0.9 -> 0.3%), p (down sk3 2.6 -> 2.5%, lifted 2.9 ->
  3.0% 3-point only). Doesn't change the sign-off. anim_test c22e (sl/at_qs22e.log) ALL PASSED, same as c20c.
  Part 3 blobs written: animator f6d27b0464d3d37f60e2ef2027d6d4cc120f70fb, character.h
  1cb9ff7ec5cfc57a7eb39fefee182b0023245fbb (on c20c fbf469f6 / 5bdefd0e); patches qs/step3_animator_fbf469f6.patch
  (+28/-8), qs/step3_character_5bdefd0e.patch (+1), verified. In game: st28fsw running (01:31), then st30sw, st30fsw.
- 01:45 st28fsw (c20c, LOCO_FRESHTOP=1) == st28sw exactly: with the test's 1.6 m run-up on the platform the first
  step down is scanned in time anyway (the landing probe sees the drop: |ga - fz| > 0.04 starts the scan, and its
  steps reset stairSeen). -> fix_v3q.py: LOCO_FRESHTOP=2 also starts the down segments 0.35 m before the top nose.
  Trees st28g (c20c) / st30g (c22e) (v3l+v3n+v3p+v3q), bld59.sh; jobs st28gsw / st30gsw. st30fsw job dropped.
- 02:0x in game c20c st28sw -> c22e st30sw (normal): sprint down sk3 4.3 -> 0.0%, deep 9 -> 0 (the edge rule: the
  first step down's heel-edge gone); run down IDENTICAL (the quick-step +28.7 cm landing and the turn-exit drop
  remain): qsga does nothing in game (the 3-frame swing's own fz lags 2 treads, not groundAhead) -> part 3 = c22d
  (qsga DROPPED). c22d blobs written: animator 87eb70ef7c7265c7597400875ef17c2bbf4e139c, character.h 1cb9ff7e;
  patches qs/step3_*.patch (+27/-8, +1) remade for c22d, verified. st30g rebuilt as c22d (bld60), jobs st30gsw
  (FRESHTOP=2) and st30gnsw (normal). Draft qs/signoff_c22d_draft.txt.
- 02:29 near-edge start (FRESHTOP=2): c20c st28gsw == c22d st30gsw in all four cases (in game the first steps down
  from 0.35 m before the nose are scanned in time too; the late scan's gain is the harness's only). c20c near-edge:
  run down sk3 0.0%, sprint down sk3 4.2% / line lifted 4.2% / deep 6 (heel at a=1.93 near the flight's foot, pick
  refused). st30gnsw (c22d normal) running.

## 44. 10-27 02:5x PART 3 FIRST HALF SIGNED OFF (c22d) on HEAD c104038 (= snapshot 67: anim files c20c)
- Sent qs/signoff_c22d_draft.txt: blobs 87eb70ef / 1cb9ff7e on fbf469f6 / 5bdefd0e (patches qs/step3_*.patch).
  In game normal: sprint down sk3 4.3 -> 0.0%, deep 9 -> 0 (edge rule); near-edge start (FRESHTOP=2): identical;
  the late scan's gain is harness-only (first steps sunk 21/33/24/66 -> 0/0/8/16).
- Second half (the short swing after a quick step at the turn) NOT fixed: c22c release (slides 32 cm) and c22e qsga
  (no in-game effect) both dropped. Next: a diagnostic build (v3m+v3o style) at that swing (trace: run down, foot 0,
  laps 4-8, the quick step from the landing at a=2.6 -> 1.6, then a 3-frame swing landing +28.7 cm at a=0.80).
- orch49 finished 02:42 (all jobs done). No builds running.
- 03:0x lead: part 3 first half ACCEPTED for snapshot 68 (87eb70ef / 1cb9ff7e on c104038). Second half: diagnostic
  build first; if it doesn't converge in a couple of tries, sign off what was learned and park it.
- Correction: qsga (c22e) DID change the in-game swing after the quick step - its start no longer jumps 56 cm up
  (f-3 heel +56.0 -> +5.0) - but its touchdown is still +28.7 cm (fz lag), and planted-frame stats don't see swings.
- 02:5x lead FYI: main = 0e4bf3a (PR #60 = snapshot 67), tree = c104038's; part 3 blobs stay valid. st31d (diag) built 02:55, job st31dsw4.
- 03:0x DIAG st31dsw4 (log after the run: log_st31dsw4.txt), the short swing (trace 18, seg 13): quick step ends at
  gait sw 0.65 (planted 1 frame, fz = ga with qsga); f-3 lifted, sw 0.74, no scan (not lateDown yet, > 0.6); f-2
  sw 0.83: ga dropped -> late first scan asked (bits 5); f-1 taken in (bits 56: steps, late), lsh1; f-0 touchdown:
  pick why 5 - the heel 5.7 cm BEHIND the scan's start (u -0.057): the foot came down ~22 cm short of where the late
  prediction had it (scanFrom = predicted mid - 0.25). Samples under it flat at the right tread (0.51).
  -> try: late scans start further back (X_LBACK, harness first).
- 03:3x c22g (= c22e + patches/qslate.py: qsSwing flag set at a quick step's end inside the gait's swing; that
  swing's late scan aimed 12 cm short) - harness identical to c22e; in game diag st32dsw4: the +28.7 cm landing
  (trace 18 of st31dsw4) GONE; run down lifted 10.6 -> 9.6% (line 1.5 -> 0.5%), rest identical. -> evidence run
  st33 (c22g + v3n/v3p/v3q, 8 laps) vs st30gnsw (c22d).
- second half candidate = c22g on c22d: blobs written animator fddadd41bc18dfcdcd864432449e4ac0d62a5896, character.h
  75b2c03b7d196291d43b2ceaa6a0077536107f25 (on 87eb70ef / 1cb9ff7e = snapshot 68); patches qs/step3b_animator_87eb70ef
  .patch (+8/-2), qs/step3b_character_1cb9ff7e.patch (+1). Harness c22d -> c22g: full suites 1 worse (spiral.up slide
  total 14.7 -> 18.0 cm/person, 30 Hz); interior spiral down floats 397 -> 393, tight spiral 1713 -> 1719; walls down
  same or better (one lifted +0.1); rest identical. Evidence run st33sw (8 laps) queued; anim_test qs22g queued.

## 45. 10-27 04:0x PART 3 SECOND HALF SIGNED OFF (c22g) on HEAD 8b06ac7 (= snapshot 68: anim files c22d)
- Sent qs/signoff_c22g_draft.txt: blobs fddadd41bc18dfcdcd864432449e4ac0d62a5896 / 75b2c03b7d196291d43b2ceaa6a0077536107f25
  on 87eb70ef / 1cb9ff7e (patches qs/step3b_*.patch). In game c22d st30gnsw -> c22g st33sw: run down lifted 11.5 ->
  9.0% (line 3.8 -> 1.3%), traces 24 -> 19; the rest identical. anim_test sl/at_qs22g.log ALL PASSED (= c22d's).
- Clean files loco/c22g/. Option 2 done (parts 1-3); left in it: the turn-exit nose landing (pick why 3).
- Next in the plan: option 1 (the stop float) unless the lead says otherwise.

## 46. 10-27 04:1x PARKED by the lead (stairs series closed; usage tight). Nothing of mine running.
- Lead: c22g ACCEPTED for snapshot 69 (animator fddadd41, character.h 75b2c03b; on 8b06ac7). Next time: the hand-built
  interiors' stairs and doors.
- DONE (option 2, all in snapshots): part 1 c18 (flat landing, snap 65), part 2 c20c (nose landings + toe-off roll +
  heel probe at the heel + the nose found by bisecting probes; snap 67, addendum sent: pl/nj/p fine), part 3 first
  half c22d (late first scan for the touchdown pick + the edge rule; snap 68), second half c22g (a quick step's
  touchdown resets groundAhead; its catch-up swing's late scan aimed 12 cm short; snap 69).
  Blob chain: a106b463/9e3388ff (c18) -> fbf469f6/5bdefd0e (c20c) -> 87eb70ef/1cb9ff7e (c22d) -> fddadd41/75b2c03b (c22g).
- OPEN KNOWN LIMITS (for later):
  1. Running down, the second step onto a flight after the landing's turn: the pick refuses (why 3: the swing's scan
     10 cm across where the foot came down, turned 18 degrees -> its nose estimate wrong); the foot lands on the right
     tread then drops onto the one below (heel 17 cm into the step, 2 frames, 5 of 8 laps; the run-down "10 deep").
     Tried: a probe-based nose search for unpicked touchdowns (patches/nosesearch.py, X_NS) -> 17 cm floats at stops.
     Idea not tried: use two scans to get the riser direction, or relax |v| when the scan dir is close to the flight's.
  2. The 12 cm late-scan offset is fitted to one in-game case (catch-up 22 cm short).
  3. The full suite's spiral walker going up (30 Hz): slide total 14.7 -> 18.0 cm/person (the quick step's ground).
  4. Older: tight spiral down floats 51 -> 64 (60 Hz), the roll's 3-8 mm sole dips, the near-edge-start test's step
     near the foot of flight 2 (pick refused), the toe tip past the outer wall at the turn (c16), option 1 the stop
     float (stairs17.stops.up, pre-existing) never started.
- STATE ON DISK: clean anim files loco/c18, c20, c20c, c21, c22a, c22d, c22e, c22g; harness trees qs/ctree22..33 (lm_c20..lm_c22g
  in sl/); game trees locotour/st24n st28* st30* st31d st32d st33 (test blocks v3l + fix_v3n/p/q/m/o/r.py); logs
  locotour/log_st*.txt; scripts lpcmp2.py, trcls.py, qs/atcopy.sh (anim_test on a copy), o2bcmp/o2sink/evsum2/
  kitcmp2/plsum/sbwcmp. attree64 holds 5804e7d's anim files (restored after the 00:45 restart). orch49.sh / jobs49.txt
  for game runs (all jobs done).
