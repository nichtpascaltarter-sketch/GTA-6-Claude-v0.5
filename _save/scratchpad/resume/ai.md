# AI agent: where things stand (wrapped up 2026-10-01 21:13)

Owner of: src/game/ai.cpp, police.cpp, lanes.cpp, traffic_core.cpp, pednav.cpp, barks.cpp, traffic.cpp, pedai.cpp,
events.cpp, ai_core.h, ai_game.h, tools/traffic_sim.cpp; the "AI additions" block of game.h and gameworld.h; the
autoplay modes in app.cpp.

Not mine:
- peds.cpp and carry.cpp belong to the lead. My hand blocks below are for the lead to graft.
- population.cpp belongs to the world-life agent. I only call its pop_detail::greet* helpers.
- src/anim belongs to the animation agent; src/world is not mine.

Nothing of mine is running: no chains, builds, games or monitors.

`SP` = /tmp/claude-0/-home-user-GTA-6-Claude-v0-5/cc7ce613-fe8a-5fa0-8160-c9078e8be173/scratchpad, and `AW` = SP/aiwork.

## 1. Merged / handed over

**Set 2 is with the lead for snapshot 24.** It was signed off on nt_ai59, which is 23069e5 plus 8 blobs. The lead froze them in SP/ai_set2_blobs.txt:

| File | Blob |
|---|---|
| ai.cpp | 4aaaaf83c9c25dd882d7a86e72053be93205c361 |
| ai_game.h | 39cbe3cad0acbfdb37faaa0961b57939879db89b |
| app.cpp | ce58b661969e1f4c0b7983032a8b9ec54ad7f836 |
| barks.cpp | 2f3c495f92e2c922f591d81bd58ef72d5e5581d6 |
| events.cpp | c11dea8a2fb11aea0a4cc0e02a3236783cfd584a |
| pedai.cpp | b857ca56a51882839b5400ddac530f15b1f9576c |
| police.cpp | 58a65e7ea88f7ccd9809e8b0ccffa2e6dd30d7d8 |
| traffic.cpp | a35d366513736518e63a0c258b1b116e365e3b36 |

- Set 2 goes in without the couple block. The lead wants it, together with the reach block, in the set 3+4 sign-off once a life run has checked the hands visually.
- Set-2 evidence:
  - Logs: SP/wine_{arrest,events,takeover,pedstop,panic,life,brawl}_nt_ai59.out and SP/log_*_nt_ai59.txt.
  - Montages: AW/evidence/m59*.png.
  - Sign-off text: AW/signoff_set2.txt.

## 2. Coded but unverified (all in the shared tree's AI files, uncommitted)

The shared tree's AI files hold sets 3, 4 and 5.
- **Current state:** the blobs are recorded in **AW/blobs_wip_set5.txt** (written with `git hash-object -w`).
- **Syntax check (21:12):** `AW/ai_syntax.sh` came back clean, with no errors or warnings in my files.
- **Never built or run:** this exact state.

### Sets 3+4 (frozen as nt_ai62)
- **Exe:** bin/nt_ai62.exe, built 20:38. Its tree had both hand blocks pasted into peds.cpp.
- **Blobs:** AW/blobs_nt_ai62.txt. Its peds.cpp blob, 002cb6e, includes the two blocks.
- **Tests:** none run yet. The chain was stopped before its first game launched.
- **Contents:** see AW/signoff_set34_draft.txt.
  - Parking tickets (police_ticket, PT_SCENE).
  - Crash scenes with a patrol.
  - Beat pairs helping someone down.
  - Arrest fixes: escortWait, the transport pulls over when held up within 60 m, units go on foot only within 45 m.
  - Hand on the head at the car door.
  - Greetings, overheard chats, the crossing button, lost, shoelace, rain, the proposal event, honks at jaywalkers.
  - The reach hook.
  - Test modes: ticket, proposal, crashscene, rain, night.

### Set 5
- **nt_ai63** (bin/nt_ai63.exe, built 21:10:50 with the hand blocks; blobs in AW/blobs_nt_ai63.txt) has:
  - EV_PANHANDLER: he asks the player; standing still gives $2, walking on gets no hard feelings.
  - Running for the bus (traffic.cpp bus_run, pedai ACT_ENTER_VEH busRun).
  - Rubbernecking: ai.cpp ai_sights, events ev_sights, the traffic.cpp cap, and Driver::hostCap/hostCapT in traffic_core/ai_core.h.
  - Near-miss startles.
  - Beat officers calling to jaywalkers.
  - Personal-space reaction (crowdT).
  - Test modes: panhandle, busrun, nearmiss, jaywalk, crowded.
- **Only in the shared tree since nt_ai63** (not in any build):
  - The "followed" reaction, plus the followed test mode.
  - A hand over the head when running in the rain.
  - The cut-off horn and shout (BK_CUT_OFF).
  - A line-of-sight check before someone runs for the bus.
  - Passers-by dropping change in the panhandler's cup (BK_GIVE_CHANGE).
  - Pedestrians asking beat officers the way (askableCop, BK_ASK_COP_WAY/BK_COP_GIVE_WAY).
  - meetBoost 3 in the jaywalk test.
- **Sign-off draft:** AW/signoff_set5_draft.txt.

### Hand blocks for peds.cpp (exact text; the lead grafts them)
Placement: GameWorld::animatePed, just before the `// foot IK: probe the ground where each foot is planted...` block, after the escort hand-hold.
- The couple block goes first, then the reach block.
- The same text is in AW/couple_block.txt and AW/reach_block.txt.
- AW/ai_hands.py pastes both into a frozen tree, and skips any block that is already there.

```cpp
    // a couple walking hand in hand (pedai.cpp: PedAI::handWith on both, renewed every frame by the companion): each
    // one's inner hand on the point between them at hand height (the animator picks the hand on that side)
    if (in.grabWeight <= 0.f) {
        int self = (int)(&p - peds.data());
        const PedAI* q = self >= 0 && self < (int)ai.ped.size() && ai.ped[self].uid == p.uid ? &ai.ped[self] : nullptr;
        int o = q ? q->handWith : -1;
        if (o >= 0 && o < (int)peds.size() && o < (int)ai.ped.size() && peds[o].used && peds[o].uid == q->handUid && ai.ped[o].uid == peds[o].uid &&
            ai.ped[o].handWith == self && time - q->handT < 0.3 && p.state == PS_ONFOOT && peds[o].state == PS_ONFOOT && !p.ragdoll && !peds[o].ragdoll) {
            vec3 j = rel(peds[o].pos, p.pos) * 0.5f;
            in.grabTarget = vec3(dot(vec2(j.x, j.y), rightV), dot(vec2(j.x, j.y), fwd) + 0.05f, 0.84f);
            in.grabWeight = 1.f;
        }
    }
    // an AI hand out to a point (pedai.cpp / police.cpp: PedAI::reachAt while reachT is ahead - a beat officer's parking
    // ticket going under the wiper); the animator picks the hand on that side
    if (in.grabWeight <= 0.f && p.state == PS_ONFOOT && !p.ragdoll) {
        int self = (int)(&p - peds.data());
        const PedAI* q = self >= 0 && self < (int)ai.ped.size() && ai.ped[self].uid == p.uid ? &ai.ped[self] : nullptr;
        if (q && time < q->reachT) {
            vec3 j = q->reachAt - p.pos.toVec3();
            in.grabTarget = vec3(dot(vec2(j.x, j.y), rightV), dot(vec2(j.x, j.y), fwd), j.z);
            in.grabWeight = 1.f;
        }
    }
```

## 3. Scripts, exes, logs (AW = SP/aiwork)

| Script | What it does |
|---|---|
| ai_buildtree.sh OUTEXE | Freezes src, tools, build.sh and build/gen into SP/tree_TAG and records the blobs from that copy into AW/blobs_TAG.txt. Then: memgate 2000, syntax check (exit 2 if the error is mine, 3 if foreign), memgate 3200, `QUICK=1 build.sh`, moves the exe to bin/. With `HANDS=1` it first pastes the hand blocks with ai_hands.py. Writes its PID to AW/ai_buildloop.pid. |
| ai_refreshtree.sh TAG | Copies the current AI files into a frozen tree that is still queued. It refuses once a cc1plus is running under the build's PID. |
| ai_runtests.sh EXE mode:dur:renderevery:autoevery ... | Sequential games through tools/run.sh in the Wine prefix /tmp/wine_ai. Memgate 3000. Shots go to /tmp/ai/MODE/ (its BMPs are cleared at start). Writes SP/wine_MODE_TAG.out and SP/log_MODE_TAG.txt, and prints a "MODE rc=..." line per run. |
| ai_detach.sh LOG CMD... | Runs a command with setsid nohup and prints its PID. |
| ai_memgate.sh N, ai_syntax.sh [TREE] | My memory gate, and the syntax check (needs memfree of at least 2000; gives up after 10 min). |
| chain_set34.sh, chain_set34b.sh, chain_set5.sh | The queued chains, now stopped. **Their `kill -0 PID` waits name old PIDs** (16492, 25214, 25290), which may belong to other processes after a restart. Edit or remove those lines before reusing them. |

- Exes: bin/nt_ai62.exe (sets 3+4) and bin/nt_ai63.exe (set 5 without the later additions). nt_ai59/60/61 are deleted.
- Logs:
  - Build logs: AW/build_nt_ai*.out.
  - Chain logs: AW/run_chain*.log.
  - Older montages: /tmp/ai/MODE/*.png; the set-2 ones are copied to AW/evidence/.

## 4. Next steps (rules: QUICK=1 builds; at most one build and one Wine run of mine at a time; memfree gates of 3200 for builds, 3000 for runs and 2000 for syntax checks)

1. **Sets 3+4 on nt_ai62.** Run:
   `AW/ai_detach.sh AW/run_chain_set34.log AW/ai_runtests.sh bin/nt_ai62.exe ticket:150:12:60 proposal:150:12:60 crashscene:150:12:60 life:200:12:100 rain:90:12:100 hurt:150:12:30 arrest:260:12:60 pedstop:240:12:60 events:240:12:60 night:200:12:100`
   Then run crowd:28.5:12:100 and soak:300:12:100.
   Check each:
   - ticket: the wiper hand; the owner reads the ticket and drives off.
   - proposal: yes, then no; the ring.
   - crashscene: the unit pulls over behind; officer roles.
   - life: couples "hand in hand" more than 0 and **the hands visually**; greetings; "asked okay" after t=140.
   - rain: shelters.
   - hurt: copAid.
   - arrest: **stage 3 now completes in 260 s?** Tell the lead the result.
   - crowd: AI ms. soak: traffic health.
   Then send "AI snapshot-safe, sets 3+4" using AW/signoff_set34_draft.txt, the nt_ai62 blobs, and both hand blocks (§2).
2. **Set 5.** Build nt_ai64 from the shared tree:
   `HANDS=1 AW/ai_detach.sh AW/build_nt_ai64.out env HANDS=1 AW/ai_buildtree.sh bin/nt_ai64.exe`
   Then run:
   `AW/ai_runtests.sh bin/nt_ai64.exe panhandle:110:12:60 busrun:170:12:60 jaywalk:120:12:60 crowded:90:12:60 followed:100:12:60 nearmiss:120:12:30 rain:90:12:100 crashscene:150:12:60 life:200:12:100 soak:300:12:100`
   Check the census totals: "bus runs (made/missed)", "change (passers-by)", "gawks", "near misses", "jaywalk calls", "crowded (left)", "followed (sharp)", "cut offs". Also check the LOG lines "bus:", "events: panhandler", "police: officer N calls to ped", "street meet: ... asks officer".
   Then sign off with AW/signoff_set5_draft.txt. After the sets are signed off, delete nt_ai62/63.
3. Keep developing in the shared tree. Ideas: drivers turning their heads at scenes (would need a peds.cpp hunk from the lead); more ambient events.
