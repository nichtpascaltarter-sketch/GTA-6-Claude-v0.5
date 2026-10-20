# EV_BRAWL: a fight breaking out on the sidewalk (events.cpp, ai_game.h, barks.cpp, app.cpp test hook)
import sys
root='/home/user/GTA-6-Claude-v0.5/src/game/'
def patch(fn, pairs):
    p=root+fn
    s=open(p).read()
    for old,new in pairs:
        if s.count(old)!=1:
            print("FAIL", fn, s.count(old), old[:90]); sys.exit(1)
        s=s.replace(old,new)
    open(p,'w').write(s)

patch('ai_game.h', [(
'''    BK_SUSPECT, BK_COP_ESCORT,                        // in cuffs on the way to the car, and the officer walking them
    BK_COUNT''',
'''    BK_SUSPECT, BK_COP_ESCORT,                        // in cuffs on the way to the car, and the officer walking them
    BK_BRAWL, BK_BRAWL_FRIEND,                        // squaring up on the sidewalk, and the friend trying to calm it
    BK_COUNT''')])

patch('barks.cpp', [(
'''#define BANK(k, arr) {k, arr, (int)ARRAY_COUNT(arr)}''',
'''const Line kBrawl[] = {{"[angry:0.7]You got a problem with me?", 0}, {"[angry:0.7]Say that again. Say it again!", 0}, {"[angry:0.6]Back up out my face!", 0},
                       {"[angry:0.7]You want to go? Let's go!", 0}, {"[angry:0.6]You spilled my drink, man!", 0}, {"[angry:0.6]Who you think you're talking to?", 0},
                       {"[angry:0.6]Keep walking, I'm not playing!", 0}, {"[angry:0.7]Que te pasa? Eh? Que te pasa?", LB_LUNA}};
const Line kBrawlFriend[] = {{"[calm:0.5]Let it go, man. He's not worth it.", 0}, {"[calm:0.5]Come on, walk away. Walk away.", 0}, {"[scared:0.4]Not here, man, the cops...", 0},
                             {"[calm:0.5]Hey, hey, hey, chill. Both of you.", 0}, {"[calm:0.4]Dejalo, hermano, no vale la pena.", LB_LUNA}};

#define BANK(k, arr) {k, arr, (int)ARRAY_COUNT(arr)}'''),(
'''    BANK(BK_COP_ESCORT, kCopEscort),
};''',
'''    BANK(BK_COP_ESCORT, kCopEscort), BANK(BK_BRAWL, kBrawl), BANK(BK_BRAWL_FRIEND, kBrawlFriend),
};''')])

patch('events.cpp', [(
'''    EV_TAKEOVER,       // a street takeover: donuts in a crossing, cars across two approaches, a crowd filming and cheering;
                       // it breaks up when the police show (somebody always calls them) - the driver makes a run for it
    EV_COUNT''',
'''    EV_TAKEOVER,       // a street takeover: donuts in a crossing, cars across two approaches, a crowd filming and cheering;
                       // it breaks up when the police show (somebody always calls them) - the driver makes a run for it
    EV_BRAWL,          // a fight breaking out on the sidewalk: two men shouting in each other's faces (a friend trying to
                       // walk one away), mostly fists; the crowd backs off and films, somebody calls it in - the police
                       // come for the one who started it (run down, cuffed and taken away)
    EV_COUNT'''),(
'''        w[EV_TAKEOVER] = !calmOnly && pinfo.wanted == 0 && urban ? ((tod > 19.5f || tod < 3.5f) ? 1.1f : 0.12f) : 0.f;''',
'''        w[EV_TAKEOVER] = !calmOnly && pinfo.wanted == 0 && urban ? ((tod > 19.5f || tod < 3.5f) ? 1.1f : 0.12f) : 0.f;
        w[EV_BRAWL] = !calmOnly && pinfo.wanted == 0 && urban ? (night ? (nightlife ? 1.4f : 0.6f) : 0.18f) : 0.f;'''),(
'''                    // ---------------------------------------------------------------- mugging
                    case EV_MUGGING: {''',
'''                    // ---------------------------------------------------------------- a fight breaking out
                    case EV_BRAWL: {
                        WalkSpot ws;
                        if (!sidewalkSpot(*this, ringPoint(ha, pp, fwd, 40.f, 90.f), 30.f, ws)) break;
                        const AI::WalkLink& L = laneGraph.walkLinks[ws.link];
                        if (L.kind != AI::WL_SIDEWALK || L.length < 10.f) break;
                        vec3 apos = walkOffset(*this, ws, -0.62f, 0.f);
                        vec3 bpos = walkOffset(*this, ws, 0.62f, 0.f);
                        if (!hiddenFrom(*this, apos, 30.f, warm) || !hiddenFrom(*this, bpos, 30.f, warm)) break;
                        int a = spawnActor(*this, apos, 0.f, ha & ~1u, 0, FAC_CIVILIAN, PR_CIVILIAN, evId);   // (even seeds: men)
                        int bb = a >= 0 ? spawnActor(*this, bpos, 0.f, hash32(ha + 3u) & ~1u, 0, FAC_CIVILIAN, PR_CIVILIAN, evId) : -1;
                        if (bb < 0) {
                            if (a >= 0) despawnPed(a);
                            break;
                        }
                        // a friend of the first, a step behind him, trying to walk him away
                        vec3 fpos = walkOffset(*this, ws, -1.5f, ws.halfWidth * 0.45f);
                        int fr = spawnActor(*this, fpos, 0.f, hash32(ha + 5u), 0, FAC_CIVILIAN, PR_CIVILIAN, evId);
                        setActor(*this, a, evId, apos.xy(), yawTowards(apos.xy(), bpos.xy()), 7, -1);
                        setActor(*this, bb, evId, bpos.xy(), yawTowards(bpos.xy(), apos.xy()), 7, -1);
                        peds[a].yaw = yawTowards(apos.xy(), bpos.xy());
                        peds[bb].yaw = yawTowards(bpos.xy(), apos.xy());
                        pedAI(a).temper = 2;
                        pedAI(bb).temper = 2;
                        e.ped[0] = refPed(*this, a);
                        e.ped[1] = refPed(*this, bb);
                        e.np = 2;
                        if (fr >= 0) {
                            setActor(*this, fr, evId, fpos.xy(), yawTowards(fpos.xy(), apos.xy()), 0, -1);
                            peds[fr].yaw = yawTowards(fpos.xy(), apos.xy());
                            e.ped[2] = refPed(*this, fr);
                            e.np = 3;
                        }
                        e.pos = (apos + bpos) * 0.5f;
                        e.dir = ws.t;
                        e.flag = (ha >> 11) % 4u != 0u;   // three in four come to blows
                        e.barkT = 0.6f;
                        e.fxT = 3.f;
                        ok = true;
                        break;
                    }
                    // ---------------------------------------------------------------- mugging
                    case EV_MUGGING: {'''),(
'''            // stage A: racing to a far destination
            case EV_RACERS: {''',
'''            // stage A: the shouting, in each other's faces (pointing, a friend trying to walk one away); B: the fight
            // (fists - the crowd backs off and films; somebody calls it in); C: over - everybody goes their way, and the
            // police, called, come looking for the one who threw the first punch
            case EV_BRAWL: {
                int da = livePed(*this, e.ped[0]), db = livePed(*this, e.ped[1]), fr = e.np > 2 ? livePed(*this, e.ped[2]) : -1;
                if (da < 0 || db < 0) {
                    over = true;
                    break;
                }
                if (e.stage == ST_A) {
                    if (!calmActor(*this, da) || !calmActor(*this, db)) {   // (scared off, or the player broke it up)
                        setStage(e, ST_C);
                        break;
                    }
                    // closing in on each other as it heats up
                    vec2 pa2 = peds[da].pos.toVec3().xy(), pb2 = peds[db].pos.toVec3().xy();
                    vec2 mid = (pa2 + pb2) * 0.5f, ax = normalize(pb2 - pa2 + vec2(1e-4f, 0.f));
                    float gap = Max(0.95f, 1.25f - e.t * 0.03f);
                    pedAI(da).anchor = mid - ax * (gap * 0.5f);
                    pedAI(db).anchor = mid + ax * (gap * 0.5f);
                    pedAI(da).anchorYaw = yawTowards(pa2, pb2);
                    pedAI(db).anchorYaw = yawTowards(pb2, pa2);
                    if (e.barkT <= 0.f) {
                        u32 hb = hash32((u32)(e.age * 10.f) + slot * 77u);
                        e.barkT = 2.f + hashToFloat(hb) * 1.6f;
                        int speaker = ((int)(e.age / 2.4f) & 1) ? da : db;
                        aiSay(speaker, BK_BRAWL, 0.9f, plDist < 25.f);
                        if (hb % 3u != 0u && peds[speaker].pendingAction < 0) peds[speaker].pendingAction = Anim::CLIP_POINT;
                    }
                    if (fr >= 0 && calmActor(*this, fr)) {
                        // the friend at the first one's shoulder, talking him down
                        vec2 side = AI::rightOf(ax);
                        pedAI(fr).anchor = pa2 - ax * 0.75f + side * 0.55f;
                        pedAI(fr).anchorYaw = yawTowards(pedAI(fr).anchor, pa2);
                        if (e.fxT <= 0.f) {
                            e.fxT = 4.f + hashToFloat(hash32((u32)(e.age * 3.f))) * 3.f;
                            aiSay(fr, BK_BRAWL_FRIEND, 0.8f, plDist < 25.f);
                        }
                    }
                    // the raised voices turn heads (and get the phones out)
                    if (e.t > 2.f && fmodf(e.t, 2.f) < dt) aiStimulus(dvec3(e.pos), STIM_FIGHT, da, 16.f, false);
                    if (e.flag && e.t > 10.f + hashToFloat(hash32((u32)slot + (u32)e.pos.x)) * 4.f) {
                        // a shove, and it is on
                        for (int k = 0; k < 2; k++) {
                            int me = k == 0 ? da : db, other = k == 0 ? db : da;
                            PedAI& ma = pedAI(me);
                            ma.activity = ACT_WALK;
                            ma.stance = 0;
                            peds[me].brain.type = BRAIN_COMBAT;
                            peds[me].brain.target = other;
                            peds[me].brain.timer = 0.f;
                        }
                        peds[db].pendingAction = Anim::CLIP_STAGGER;
                        aiStimulus(dvec3(e.pos), STIM_FIGHT, da, 26.f, false);
                        setStage(e, ST_B);
                    } else if (!e.flag && e.t > 18.f) {
                        setStage(e, ST_C);   // (they think better of it)
                    }
                } else if (e.stage == ST_B) {
                    if (fr >= 0 && calmActor(*this, fr) && e.fxT <= 0.f) {
                        e.fxT = 3.f;
                        aiSay(fr, BK_BRAWL_FRIEND, 0.7f, plDist < 25.f);
                    }
                    // somebody calls it in: the police come for the one who started it
                    if (!e.asked && e.t > 4.f) {
                        addCrimeIncident(*this, peds[da].pos, da);
                        e.asked = true;
                    }
                    bool aDown = isDown(peds[da]), bDown = isDown(peds[db]);
                    if (aDown || bDown || e.t > 13.f) {
                        for (int me : {da, db})
                            if (!isDown(peds[me]) && peds[me].brain.type == BRAIN_COMBAT) {
                                peds[me].brain.type = BRAIN_WANDER;
                                peds[me].brain.target = -1;
                                peds[me].brain.edge = -1;
                                pedAI(me).navOk = false;
                            }
                        setStage(e, ST_C);
                    }
                } else {
                    // over: they go their ways (the friend with the first one)
                    over = true;
                }
                break;
            }
            // stage A: racing to a far destination
            case EV_RACERS: {''')])

patch('app.cpp', [(
'''            autoplay == "places" || autoplay == "greet" || autoplay == "hurt" || autoplay == "arrest") {''',
'''            autoplay == "places" || autoplay == "greet" || autoplay == "hurt" || autoplay == "arrest" || autoplay == "brawl") {'''),(
'''            } else if (autoplay == "arrest") {''',
'''            } else if (autoplay == "brawl") {
                // a fight breaking out on a Calle Luna sidewalk at night (applyAutoplay): the shouting, the fists, the
                // crowd filming, the police coming for the one who started it
                autoDuration = 150.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 22.5f;
                game.ai.forceEvent = 12;   // (EV_BRAWL)
            } else if (autoplay == "arrest") {'''),(
'''        } else if (autoplay == "arrest") {
            // stage 1:''',
'''        } else if (autoplay == "brawl") {
            // follow the fight from across the sidewalk, then the one who started it (and the police after him)
            static float logT = 0.f, shotT = 2.f;
            static int shots = 0, starter = -1;
            static u32 starterUid = 0;
            int stage = -1;
            vec3 at;
            std::string st = game.aiBrawlText(&stage, &at, &starter, &starterUid);
            int s = starter >= 0 && starter < (int)game.peds.size() && game.peds[starter].used && game.peds[starter].uid == starterUid ? starter : -1;
            if (stage >= 0 || s >= 0) {
                vec3 P = s >= 0 ? (game.peds[s].state == PS_INVEHICLE && game.peds[s].vehicle >= 0 ? game.vehicles[game.peds[s].vehicle].sim.body.pos.toVec3() : game.peds[s].pos.toVec3()) : at;
                if (stage >= 0 && stage < 2) P = at;
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(P + vec3(5.5f, -6.5f, 3.6f));
                game.rig.scriptTarget = dvec3(P + vec3(0.f, 0.f, 1.f));
                game.rig.scriptFov = 52.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 20) {
                    shotT = 5.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_brawl_%02d_s%d", shots, stage));
                    shots++;
                }
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                std::string ss = "starter none";
                if (s >= 0) {
                    const Ped& q = game.peds[s];
                    ss = StrFormat("starter %d state %d act %d brain %d veh %d", s, (int)q.state, (int)game.ai.ped[s].activity, (int)q.brain.type, q.vehicle);
                }
                LOG("autoplay brawl t=%.1f | %s | %s | %s", t, st.c_str(), ss.c_str(), game.aiCensusText(40.f).c_str());
            }
        } else if (autoplay == "arrest") {
            // stage 1:''')])
print("patched")

patch('gameworld.h', [(
'''    std::string aiEventText(int* stage = nullptr, vec3* pos = nullptr, int* car = nullptr) const;   // events.cpp: the active takeover (tests)''',
'''    std::string aiEventText(int* stage = nullptr, vec3* pos = nullptr, int* car = nullptr) const;   // events.cpp: the active takeover (tests)
    std::string aiBrawlText(int* stage, vec3* pos, int* starter, u32* starterUid) const;              // events.cpp: the active fight (tests)''')])

patch('events.cpp', [(
'''std::string GameWorld::aiEventText(int* stage, vec3* pos, int* car) const {''',
'''std::string GameWorld::aiBrawlText(int* stage, vec3* pos, int* starter, u32* starterUid) const {
    for (const AmbientEvent& e : gEv.ev) {
        if (!e.active || e.type != EV_BRAWL) continue;
        int a = livePed(*this, e.ped[0]), b = livePed(*this, e.ped[1]), f = e.np > 2 ? livePed(*this, e.ped[2]) : -1;
        if (stage) *stage = e.stage;
        if (pos) *pos = e.pos;
        if (starter && a >= 0) {
            *starter = a;
            if (starterUid) *starterUid = peds[a].uid;
        }
        return StrFormat("brawl stage %d t %.1f at %.0f %.0f | a %d (brain %d) b %d (brain %d) friend %d | police called %d", (int)e.stage, e.t, e.pos.x, e.pos.y,
                         a, a >= 0 ? (int)peds[a].brain.type : -1, b, b >= 0 ? (int)peds[b].brain.type : -1, f, (int)e.asked);
    }
    if (stage) *stage = -1;
    return "brawl: none";
}

std::string GameWorld::aiEventText(int* stage, vec3* pos, int* car) const {''')])
print("patched 2")
