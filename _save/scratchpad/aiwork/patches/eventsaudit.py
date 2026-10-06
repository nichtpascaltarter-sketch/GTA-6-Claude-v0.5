# autoplay "events": force the ambient events one after another and log how each plays out (an audit of the event
# system: does each one stage, progress through its stages and end, does the police part work)
root='/home/user/GTA-6-Claude-v0.5/src/game/'
def patch(fn, pairs):
    p=root+fn
    s=open(p).read()
    for old,new in pairs:
        assert s.count(old)==1, (fn, old[:80])
        s=s.replace(old,new)
    open(p,'w').write(s)

patch('gameworld.h', [(
'''    std::string aiBrawlText(int* stage, vec3* pos, int* starter, u32* starterUid) const;              // events.cpp: the active fight (tests)''',
'''    std::string aiBrawlText(int* stage, vec3* pos, int* starter, u32* starterUid) const;              // events.cpp: the active fight (tests)
    std::string aiEventsText(int want, int* stage, vec3* pos) const;   // events.cpp: the active events (tests); stage/pos of the `want` type''')])

patch('events.cpp', [(
'''std::string GameWorld::aiBrawlText(int* stage, vec3* pos, int* starter, u32* starterUid) const {''',
'''std::string GameWorld::aiEventsText(int want, int* stage, vec3* pos) const {
    static const char* const kNames[EV_COUNT] = {"mugging", "purse", "crash", "racers", "chase", "shootout", "drunk", "musician",
                                                 "tourists", "breakdown", "traffic stop", "takeover", "brawl"};
    std::string out;
    if (stage) *stage = -1;
    for (const AmbientEvent& e : gEv.ev) {
        if (!e.active) continue;
        if (e.type == want) {
            if (stage) *stage = e.stage;
            if (pos) *pos = e.pos;
        }
        out += StrFormat("[%s stage %d t %.1f at %.0f %.0f |", kNames[e.type], (int)e.stage, e.t, e.pos.x, e.pos.y);
        for (int k = 0; k < e.np && k < 5; k++) {
            int id = livePed(*this, e.ped[k]);
            if (id < 0) {
                out += " -";
                continue;
            }
            const Ped& q = peds[id];
            out += StrFormat(" %d:s%d b%d a%d", id, (int)q.state, (int)q.brain.type, id < (int)ai.ped.size() ? (int)ai.ped[id].activity : -1);
        }
        out += StrFormat(" | asked %d done %d] ", (int)e.asked, (int)e.done);
    }
    return out.empty() ? std::string("events: none") : out;
}

std::string GameWorld::aiBrawlText(int* stage, vec3* pos, int* starter, u32* starterUid) const {''')])

patch('app.cpp', [(
'''            autoplay == "places" || autoplay == "greet" || autoplay == "hurt" || autoplay == "arrest" || autoplay == "brawl") {''',
'''            autoplay == "places" || autoplay == "greet" || autoplay == "hurt" || autoplay == "arrest" || autoplay == "brawl" || autoplay == "events") {'''),(
'''            } else if (autoplay == "brawl") {''',
'''            } else if (autoplay == "events") {
                // the ambient events one after another on downtown sidewalks in the afternoon (applyAutoplay)
                autoDuration = 6 * 40.f + 0.5f;
                vec2 q(2713.f, 763.f);
                p.pos = dvec3(q.x, q.y, game.groundHeight(q.x, q.y, 20.f));
                env.timeOfDay = 16.f;
            } else if (autoplay == "brawl") {'''),(
'''        } else if (autoplay == "brawl") {
            // follow the fight from across the sidewalk''',
'''        } else if (autoplay == "events") {
            // six ambient events in turn, 40 s each: forced, watched from across the street, logged as they play out
            // (a mugging, a purse snatching, a fender bender, a police chase, a breakdown, a traffic stop)
            static const int kTypes[6] = {0, 1, 2, 4, 9, 10};   // EV_MUGGING, EV_PURSE, EV_CRASH, EV_CHASE, EV_BREAKDOWN, EV_TRAFFIC_STOP
            static float logT = 0.f, shotT = 3.f;
            static int shots = 0, cur = -1;
            int slot = Min((int)(t / 40.f), 5);
            if (slot != cur) {
                cur = slot;
                game.ai.forceEvent = kTypes[slot];
                shotT = 3.f;
                LOG("autoplay events: %d - forcing event type %d (t=%.1f)", slot, kTypes[slot], t);
            }
            int stage = -1;
            vec3 at;
            std::string st = game.aiEventsText(kTypes[slot], &stage, &at);
            if (stage >= 0) {
                game.ai.forceEvent = -1;   // (that one is on: no second)
                game.rig.scriptActive = true;
                game.rig.scriptPos = dvec3(at + vec3(7.f, -8.f, 4.5f));
                game.rig.scriptTarget = dvec3(at + vec3(0.f, 0.f, 1.f));
                game.rig.scriptFov = 55.f;
                shotT -= dt;
                if (shotT <= 0.f && shots < 40) {
                    shotT = 8.f;
                    game.requestScreenshot = shotPath(StrFormat("auto_events_%02d_e%d_s%d", shots, kTypes[slot], stage));
                    shots++;
                }
            } else {
                game.rig.scriptActive = false;
            }
            logT -= dt;
            if (logT <= 0.f) {
                logT = 2.f;
                LOG("autoplay events t=%.1f | %s | %s", t, st.c_str(), game.aiCensusText(40.f).c_str());
            }
        } else if (autoplay == "brawl") {
            // follow the fight from across the sidewalk''')])
print("events audit patched")
