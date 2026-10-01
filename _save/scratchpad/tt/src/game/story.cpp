// Story missions and side activities. Each mission is a small state machine using the GameWorld mission helpers.
#include "missions.h"

namespace Game {

namespace story_detail {

// Finds a curbside spot on the road nearest to p (right side of travel), returns position and heading.
vec3 curbSpot(GameWorld& g, vec2 p, float along, float* yawOut, float sideOffset = -1.f) {
    float s = 0, side = 0;
    int e = g.roads->nearestEdge(p, 300.f, &s, nullptr, &side);
    if (e < 0) {
        if (yawOut) *yawOut = 0.f;
        return vec3(p, g.groundHeight(p.x, p.y, 200.f));
    }
    const World::RoadEdge& ed = g.roads->edges[e];
    s = Clamp(s + along, ed.cut0 + 2.f, ed.length - ed.cut1 - 2.f);
    vec3 c = ed.posAt(s);
    vec3 t = ed.tangentAt(s);
    vec3 n(t.y, -t.x, 0);
    float off = sideOffset >= 0.f ? sideOffset : ed.halfWidth - 1.3f;
    vec3 r = c + normalize(n) * off;
    if (yawOut) *yawOut = atan2f(-t.x, t.y);
    r.z = g.groundHeight(r.x, r.y, c.z + 2.f);
    return r;
}

vec3 sidewalkSpot(GameWorld& g, vec2 p, float along) {
    float s = 0, side = 0;
    int e = g.roads->nearestEdge(p, 300.f, &s, nullptr, &side);
    if (e < 0) return vec3(p, g.groundHeight(p.x, p.y, 200.f));
    const World::RoadEdge& ed = g.roads->edges[e];
    s = Clamp(s + along, 1.f, ed.length - 1.f);
    vec3 c = ed.posAt(s);
    vec3 t = ed.tangentAt(s);
    vec3 n = normalize(vec3(-t.y, t.x, 0)) * (side >= 0 ? 1.f : -1.f);
    vec3 r = c + n * (ed.halfWidth + Max(ed.sidewalk, 1.2f) * 0.5f);
    r.z = g.groundHeight(r.x, r.y, c.z + 2.f);
    return r;
}

const u32 kMariColor = 0xffcc55ffu;   // pink (ABGR packed: r in low byte)
const u32 kDexColor = 0xff66ddaau;
const u32 kTomasColor = 0xff55ccffu;

// ------------------------------------------------------------------------------------------------------------------
// Prologue: "Low Tide" - Mari gets a call from her brother Tomas, picks him up from a Calle Luna garage and brings him
// to Mama Lucha's diner while two Cuervos in a car tail them.
class MissionLowTide : public Mission {
public:
    int tomas = -1, car = -1, chaser = -1;
    vec3 garage, diner;
    const char* title() const override { return "Low Tide"; }
    const char* brief() const override {
        return "Tomas called in a panic from a garage in Calle Luna. Pick him up and get him to Mama Lucha's diner before "
               "whoever he owes money to catches up with him.";
    }
    long long reward() const override { return 1500; }
    void start(GameWorld& g) override {
        float yaw;
        garage = sidewalkSpot(g, vec2(1980.f, 2710.f), 0.f);
        diner = curbSpot(g, vec2(1240.f, 1880.f), 0.f, &yaw);
        int ci = g.randomCivilianChar(0x7011u, 0);
        tomas = g.mPed(ci, dvec3(garage), 0.f, FAC_FRIEND);
        if (tomas >= 0) {
            g.peds[tomas].invincible = true;
            g.peds[tomas].brain.type = BRAIN_SCENARIO;
            g.peds[tomas].brain.scenario = 8;
        }
        DialogueLine l;
        l.speaker = "Tomas";
        l.text = "Mari, it's me. I messed up. I need you at the old Vargas garage, right now. Please hurry.";
        l.female = false;
        l.voiceSeed = 21;
        l.color = kTomasColor;
        g.mSay(l);
        DialogueLine m;
        m.speaker = "Mari";
        m.text = "Tomas? Hey, slow down. Stay there, I'm coming.";
        m.ped = g.player;
        m.color = kMariColor;
        g.mSay(m);
        g.mObjective("Get to the ~y~garage~s~ in Calle Luna.");
        g.mTarget(garage.xy());
        g.mMarker(dvec3(garage), 2.5f);
    }
    MissionStatus update(GameWorld& g, float dt) override {
        (void)dt;
        if (tomas < 0 || !g.peds[tomas].used) {
            failReason = "Tomas is gone.";
            return MS_FAILED;
        }
        switch (stage) {
            case 0:
                if (g.playerAt(garage.xy(), g.playerVehicle() >= 0 ? 8.f : 3.f)) {
                    g.mClearMarkers();
                    g.mClearTarget();
                    car = g.playerVehicle();
                    // cutscene: Tomas runs to the car
                    std::vector<CutsceneShot> shots;
                    CutsceneShot a;
                    a.pos = dvec3(garage + vec3(6.f, -5.f, 2.2f));
                    a.target = dvec3(garage + vec3(0, 0, 1.2f));
                    a.pos2 = dvec3(garage + vec3(4.f, -6.f, 2.f));
                    a.target2 = dvec3(garage + vec3(0, 0, 1.3f));
                    a.duration = 6.f;
                    a.fov = 42.f;
                    shots.push_back(a);
                    g.mCutscene(shots);
                    g.mSay("Tomas", "They took the money I was holding for them. Two guys from the Cuervos. They think I skimmed it.", tomas, kTomasColor);
                    g.mSay("Mari", "Did you?", g.player, kMariColor);
                    g.mSay("Tomas", "No! I swear. Just get me to Lucha's, she'll know what to do.", tomas, kTomasColor);
                    next();
                }
                break;
            case 1:
                if (!g.mInCutscene() && !g.mTalking()) {
                    int pv = g.playerVehicle();
                    if (pv >= 0) {
                        int seat = g.freeSeat(pv, false);
                        if (seat > 0) g.warpPedIntoVehicle(tomas, pv, seat);
                        g.peds[tomas].brain.type = BRAIN_FOLLOW;
                        g.peds[tomas].brain.target = g.player;
                    } else {
                        g.peds[tomas].brain.type = BRAIN_FOLLOW;
                        g.peds[tomas].brain.target = g.player;
                    }
                    g.mObjective("Take Tomas to ~y~Mama Lucha's diner~s~.");
                    g.mTarget(diner.xy());
                    g.mMarker(dvec3(diner), 3.f);
                    // tail car with two Cuervos
                    int model = g.findVehicleModel(Vehicles::VC_MUSCLE, 1);
                    if (model < 0) model = g.findVehicleModel(Vehicles::VC_SEDAN, 2);
                    if (model >= 0) {
                        float yaw;
                        vec3 sp = curbSpot(g, garage.xy() - vec2(60.f, 0.f), -30.f, &yaw);
                        chaser = g.mVehicle(model, dvec3(sp + vec3(0, 0, 0.3f)), yaw);
                        if (chaser >= 0) {
                            for (int s = 0; s < 2; s++) {
                                int ci = g.randomCivilianChar(0x9000u + s, 2);
                                int gp = g.mPed(ci, dvec3(sp), yaw, FAC_ENEMY);
                                if (gp < 0) continue;
                                g.warpPedIntoVehicle(gp, chaser, s);
                                g.giveWeapon(gp, WPN_PISTOL, 60);
                                g.peds[gp].weapon = WPN_PISTOL;
                                g.peds[gp].brain.type = BRAIN_COMBAT;
                                g.peds[gp].brain.target = g.player;
                                g.peds[gp].brain.accuracy = 0.25f;
                            }
                            g.vehicles[chaser].color0 = vec3(0.05f);
                            g.mBlipVehicle(chaser, UI::BLIP_ENEMY);
                        }
                    }
#ifdef HAVE_AUDIO
                    Audio::setScore(0x10u, 0.55f);
#endif
                    g.mSay("Tomas", "Mari... that black car. It's them!", tomas, kTomasColor);
                    next();
                }
                break;
            case 2: {
                bool tomasWithPlayer = g.peds[tomas].vehicle >= 0 && g.peds[tomas].vehicle == g.playerVehicle();
                if (g.playerAt(diner.xy(), 10.f) && (tomasWithPlayer || g.playerVehicle() < 0)) {
                    g.mClearMarkers();
                    g.mClearTarget();
                    g.mObjective("");
                    int pv = g.playerVehicle();
                    if (pv >= 0 && g.vehicles[pv].sim.speed() > 2.f) {
                        g.help("Stop the vehicle at the diner.", 2.f);
                        break;
                    }
                    if (g.peds[tomas].vehicle >= 0) g.removePedFromVehicle(tomas, true);
                    g.mSay("Mari", "Inside. Now. And Tomas - you're telling Lucha everything.", g.player, kMariColor);
                    g.mSay("Tomas", "Everything. I promise.", tomas, kTomasColor);
                    next();
                }
                break;
            }
            case 3:
                if (!g.mTalking() && stageTime > 1.f) {
                    g.peds[tomas].invincible = false;
                    g.peds[tomas].brain.type = BRAIN_GOTO;
                    g.peds[tomas].brain.goal = dvec3(diner + vec3(0, 4.f, 0));
                    g.peds[tomas].brain.speed = 1.6f;
                    g.storyBriefText = "Mama Lucha agreed to hide Tomas at the diner. The Cuervos will be back - and someone is paying them.";
                    return MS_PASSED;
                }
                break;
        }
        return MS_RUNNING;
    }
};

}  // namespace story_detail

using namespace story_detail;

void MissionManager::registerAll(GameWorld& g) {
    defs.clear();
    MissionDef d;
    d.id = "low_tide";
    d.contact = "Tomas";
    d.letter = 'T';
    d.startPos = sidewalkSpot(g, vec2(1545.f, 2330.f), 14.f).xy();
    d.storyIndex = 0;
    d.requiresFlag = -1;
    d.setsFlag = 0;
    d.repeatable = false;
    d.timeFrom = d.timeTo = 0.f;
    d.icon = UI::BLIP_MISSION;
    d.create = [] { return (Mission*)new MissionLowTide(); };
    defs.push_back(d);
}

}  // namespace Game
