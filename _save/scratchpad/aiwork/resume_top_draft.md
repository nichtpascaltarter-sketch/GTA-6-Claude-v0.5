# AI agent: where things stand (wind-down, 2026-10-06 [[TIME]] UTC)

This section is the current state. Everything below "History" is the older running log, kept for detail. It is superseded wherever it disagrees with this section.

Owner of: src/game/ai.cpp, police.cpp, lanes.cpp, traffic_core.cpp, pednav.cpp, barks.cpp, traffic.cpp, pedai.cpp, events.cpp, ai_core.h, ai_game.h, tools/traffic_sim.cpp; the AI block of game.h and gameworld.h; the autoplay modes in app.cpp; and Wildlife::leashHand (additive, read-only).

Not mine:
- peds.cpp and carry.cpp belong to the lead. I hand over hunks or whole-file blobs.
- population.cpp belongs to the world-life agent.
- src/anim belongs to the animation agent.
- src/world is not mine.

**Nothing of mine is running:** no queue runner, games, builds, chains or waiters ([[CHECK]]).

`SP` = /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad, and `AW` = SP/aiwork.

## 1. Signed off

| Sets | State | Where |
|---|---|---|
| 2-9 | Merged (snapshots 24-38; sets 5-9 = snapshot 38, PR #38). | History below. |
| 10-13 | Merged as snapshot 42 (PR #42, main 894e7fb). Known gaps reported: hitped and nearmiss (§2.2 and §2.3), and night chats. | AW/signoff_set1013_send.txt, AW/set1013_results.txt |
| 14-16 | Sign-off sent [[SENT]]: one graft on 894e7fb, 6 AI blobs plus the peds.cpp blob dbe0046 (couple grabAxis line). Waiting for the coordinator's merge. | AW/signoff_set1416_send.txt, AW/set1416_results.txt, AW/graft86.txt |

The sets 14-16 blobs (each holds sets 3-16; traffic.cpp and traffic_core.cpp are unchanged from 894e7fb):

| File | Blob |
|---|---|
| ai.cpp | 51ccb8791da7269c1e664519c0432ae552efb67c |
| ai_game.h | 036e52e96d053db3f1b07efcd86c6c96d9ee2268 |
| app.cpp | d8ab90d326f6ec25676943873511101549ea56c7 |
| barks.cpp | 57720843ed3141daaaa398df6cc5649680f712e6 |
| pedai.cpp | b79602b7255beb3e01f97b49d3dd9cd28c53d97d |
| police.cpp | 27a7a430ac9cb84bcb1ac5a100602ac72e60a334 |
| peds.cpp (the lead's) | dbe0046df9d5d184571d212439191928fb836086 = 5c6e2ff plus `in.grabAxis = vec3(0.f, 1.f, 0.f);` after `in.grabWeight = 1.f;` in the couple block |

## 2. Unfinished work and where it lives

The main tree (uncommitted) holds the sets 14-16 blobs, with set 17 on top in three files:
- ai.cpp is 0eaeb61 and ai_game.h is baa7e91; both are the 14-16 blob plus set 17.
- events.cpp is 5a754b2 (set 17 only).
- app.cpp, barks.cpp, pedai.cpp and police.cpp equal the 14-16 blobs.
- peds.cpp in the tree is HEAD's 5c6e2ff. The grabAxis line is only in blob dbe0046.

The main tree with set 17 passed a syntax check at 16:12 (AW/syntax_set17.out, no errors or warnings). It has never been built or run.

### 2.1 Set 17: event actors get in through the door (written, parked; not built or tested)
- ai.cpp ai_board::beginThen(g, id, veh, seat, then). PedAI::boardThen (ai_game.h) says what happens once the ped is in:
  - 0: the activity's own follow-up (ai_board::boarded).
  - 1: drive away (DRIVER role, unparked, engine on, attachTraffic).
  - 2: nothing; events.cpp carries on when it sees them in.
- begin() is beginThen(..., 0). boarded() reads and resets boardThen, and so does the cancel path in step().
- Two LOG lines:
  - "ai: ped N into car V (seat S) through the door: a X s clip (activity A)"
  - "ai: ped N in car V (seat S) after its door clip (X s in PS_ENTERING)"
  - The cancel line: "... the car gone or its seat taken: back on foot".
  - These lines give the exact clip lengths that nt_ai86 could only bracket from the 2 s status lines.
- events.cpp:
  - EV_CRASH: both drivers walk back to their own cars, skipped while PS_ENTERING, then beginThen 1 at the door.
  - EV_BREAKDOWN: the driver, beginThen 1; role VR_TRAFFIC is set once in.
  - EV_TRAFFIC_STOP stage ST_D: the officer, beginThen 2. The event's post-warp code runs once it sees the officer in.
- Kept instant on purpose:
  - EV_TAKEOVER runners (fleeing the police).
  - The arrested crook put into the cruiser (events.cpp ~2214, a 25 m put-in; it needs the escort system).
  - Spawn warps.
  - police.cpp bringing the car round to an escort (~3371) and the jump back in during a chase (~3451).
- BRAIN_FOLLOW (mission followers) is NOT converted. If the player drove off during the clip, the follower would be left behind. It needs either a hold on the player's car while a follower is at its door, or staying instant. The coordinator knows.
- AW/graft87.txt is an older list (b49bc7f + the set 17 tree at 12:42). Its nt_ai87 build never finished (died at the 12:54 worker restart), so there is no exe. Regenerate the list with mkgraft.sh.

### 2.2 hitped gap (sets 10-13; not written)
The victim walked off before the sorry driver got to them, and stage 2 never had a hit.

Fix plan:
- pedai.cpp ACT_ROADRAGE sorry branch: a victim who is up again waits, facing the driver, for up to ~8 s while the driver walks over.
- app.cpp hitped: put the walker inside the car's stopping distance, so stage 2 gets its hit.

### 2.3 nearmiss test lane (sets 10-13; not written)
app.cpp nearmiss picked a highway lane. Exclude highway and ramp lanes in its lane pick.

### 2.4 BRAIN_GOTO easing (not started; depends on the animation agent)
- peds.cpp updatePeds' ped-ped separation pushes positions apart but leaves p.vel alone. So a walker held up behind someone keeps its speed: its feet drag, and aiWalkRound's stuck test (ai.cpp ~161, length(p.vel) < 0.15) never fires.
- Order:
  1. The animation agent fixes the physics side: the separation removes the closing velocity.
  2. When they sign off, the coordinator asks me to run crowded, crossing, walkby, life and busrun on their exe. Watch queues, crossings and companions walking abreast.
  3. Then my part: BRAIN_GOTO walkers ease off behind a slower walker ahead, as pednav's social force does on sidewalks, instead of pressing into them.

### 2.5 Smaller notes
- nightwalk on nt_ai86: [[NIGHTWALK]].
- A hook for the UI owner, proposed in the 14-16 sign-off and not done: a Tidegram event for a car show with three or more filming (TE_CAR_SHOW).
- The couple clasp (14-16 results): close up, each hand's fingers close into a ring beside the other's. A true interlock would be a finger pose for the animation agent.
- Backlog ideas, not started:
  - drivers turning their heads at scenes (needs a peds.cpp hunk);
  - more ambient events;
  - nightlife groups' own loud stroll lines.

## 3. Next steps, in order
1. Wait for the coordinator's verdict on sets 14-16 and fix anything raised.
   - Once merged, delete bin/nt_ai86.exe if the wind-down has not already.
   - The main tree then differs from main only by set 17.
2. Set 17:
   - Graft it on the then-current main: `AW/mkgraft.sh <main> AW/graft88.txt`, then `AW/ai_buildgraft.sh <main> AW/graft88.txt bin/nt_ai88.exe`. Add the dbe0046 peds.cpp blob to the list if the grabAxis line is not merged yet.
   - Run crashscene, events and a breakdown. Check that the drivers walk to their own doors, the clip runs (the LOG lines), the doors shut, and they drive off; and that the traffic-stop officer gets back in and the stop ends.
   - Fold §2.2 and §2.3 into the same set (app.cpp and pedai.cpp, small), with hitped and nearmiss reruns.
   - Then sign off, listing the files shared with sets 14-16 (every blob holds all earlier sets).
3. When the animation agent signs off the separation fix: the check runs in §2.4, step 2.
4. Then BRAIN_GOTO easing (§2.4, step 3).
5. Then BRAIN_FOLLOW door entries, if wanted (§2.1).

## 4. How to resume
- Rules:
  - C++17, no engines or third-party code, original IP, no placeholders or TODOs.
  - Unity build: helpers go in named namespaces; never name identifiers `near` or `far` (nor `free` or `in`, which clash in places).
  - Do not commit or push; the lead integrates from my blobs.
  - Launch games only through tools/run.sh (my scripts do) and builds only through build.sh.
  - Memory gates (tools/memfree.sh): builds 3200, runs 3000, syntax checks 2000. Rerun on rc 137.
  - QUICK=1 test builds. At most one build and one Wine run of mine at a time.
  - Wine prefix /tmp/wine_ai.
  - Delete test exes and BMPs once looked at; keep PNGs.
  - Kill only PIDs traced to my own scripts. Never use pgrep, or a ps|grep pattern that can match its own command line (use a bracket trick such as `ai_queu[e]`).
- Exes: bin/nt_ai86.exe (b49bc7f + graft86: sets 10-16 + peds dbe0046) [[EXE]]. No other test exes of mine are left.
- Build an exact graft (never ai_buildtree.sh while other agents keep WIP in the shared tree):
  1. `AW/mkgraft.sh COMMIT AW/graftNN.txt` lists my AI files that differ from COMMIT, written into the object store.
  2. `AW/ai_detach.sh AW/build_nt_aiNN.out AW/ai_buildgraft.sh COMMIT AW/graftNN.txt bin/nt_aiNN.exe` does git archive, applies the blobs, runs ai_hands.py, a syntax check at the 2000 gate, then build.sh at the 3200 gate, and records AW/blobs_nt_aiNN.txt.
- Syntax-check the main tree: `AW/ai_syntax.sh` (or `AW/ai_syntax.sh TREE`).
- Test queue:
  - Start the runner: `AW/ai_detach.sh /dev/null bash -c "exec AW/ai_queue.sh >> AW/run_queue.log 2>&1"`. It runs AW/queue.txt lines "bin/EXE MODE:DUR:RENDEREVERY:AUTOEVERY[:WxH]" one at a time through AW/ai_runtests2.sh, and ends after 15 idle minutes.
  - Edit the queue only under the lock: `flock AW/queue.lock python3 AW/qedit.py add|top|drop|show ...`.
  - Results: AW/run_queue.log ("MODE rc=... wall=... shots=..."). The game log is copied to SP/log_MODE_TAG.txt at the end; while running, read /tmp/wine_ai/drive_c/users/root/AppData/Local/NeonTide/log.txt.
  - Shots go to /tmp/ai/MODE_TAG/. Make a montage with `python3 AW/mont.py DIR OUT.png N COLS 'PATTERN' TILEWIDTH`.
  - Wait with until-loops on run_queue.log (foreground waits of 600 s at most), never sleep chains.
- Test modes that matter next (app.cpp --autoplay):
  - Set 17: crashscene, events.
  - Sets 10-13 gaps: hitped, nearmiss.
  - Separation check: crowded, crossing, walkby, life, busrun.
  - Sets 14-16 regressions: rideoff, couple, copbreak, showoff, crosswalk, runby, nightwalk.
  - Durations used: rideoff 150, couple 120, copbreak 150, showoff 120, crosswalk 110, runby 150, nightwalk 150, all at :12:60 (960x540 for the shot sheets).
- The rest of AW (old chains, build logs, blob lists and sign-off texts) is history. Its old chain scripts name dead PIDs; do not rerun them.

# History (older running log; superseded by the section above where they differ)

