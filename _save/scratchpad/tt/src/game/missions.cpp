// Mission runtime: start triggers, per-frame script update, dialogue playback (TTS + subtitles), cutscene camera,
// objective markers, pass/fail flow and cleanup of mission entities.
#include "missions.h"

namespace Game {

MissionManager gMissions;

namespace mission_detail {

struct TrackedBlip {
    int ped = -1, vehicle = -1;
    UI::BlipIcon icon = UI::BLIP_OBJECTIVE;
    u32 color = 0;
};
std::vector<TrackedBlip> gTracked;
bool gHasTarget = false;
vec2 gTarget;
UI::BlipIcon gTargetIcon = UI::BLIP_OBJECTIVE;
Render::Model* gMarkerModel = nullptr;

void buildMarkerModel(Render::Renderer* r) {
    if (gMarkerModel) return;
    MeshData m;
    const int seg = 40;
    u32 col = packRGBA8(1.f, 1.f, 1.f, 1.f);
    u32 mat = makeMat(MAT_EMISSIVE);
    // open cylinder (both faces) of radius 1, height 1; alpha fades upward via vertex color alpha
    for (int side = 0; side < 2; side++) {
        u32 start = (u32)m.verts.size();
        for (int i = 0; i <= seg; i++) {
            float a = kTwoPi * i / seg;
            vec3 n(cosf(a), sinf(a), 0);
            if (side) n = -n;
            vec3 t(-sinf(a), cosf(a), 0);
            m.addVertex(vec3(cosf(a), sinf(a), 0), n, t, vec2((float)i / seg, 0), packRGBA8(1, 1, 1, 1.f), mat);
            m.addVertex(vec3(cosf(a), sinf(a), 1.f), n, t, vec2((float)i / seg, 1), packRGBA8(1, 1, 1, 0.05f), mat);
        }
        for (int i = 0; i < seg; i++) {
            u32 a = start + i * 2, b = a + 2;
            if (side) m.quadIdx(a, a + 1, b + 1, b);
            else m.quadIdx(a, b, b + 1, a + 1);
        }
    }
    (void)col;
    gMarkerModel = r->dynamic->createModel(m);
}

}  // namespace mission_detail

using namespace mission_detail;

bool GameWorld::missionActive() const { return gMissions.active != nullptr; }

std::string GameWorld::missionBrief() const {
    if (gMissions.active) return std::string(gMissions.active->title()) + "\n\n" + gMissions.active->brief();
    return storyBriefText.empty() ? std::string("No active mission. Look for mission contacts marked on the map.") : storyBriefText;
}

int GameWorld::mPed(int charIndex, dvec3 pos, float yaw, Faction f) {
    int id = spawnPed(charIndex, pos, yaw, f);
    if (id >= 0) {
        peds[id].persistent = true;
        gMissions.peds.push_back(id);
    }
    return id;
}

int GameWorld::mVehicle(int model, dvec3 pos, float yaw) {
    int id = spawnVehicle(model, pos, yaw, false);
    if (id >= 0) {
        vehicles[id].persistent = true;
        gMissions.vehicles.push_back(id);
    }
    return id;
}

void GameWorld::mObjective(const std::string& text) {
    hudObjective = text;
#ifdef HAVE_AUDIO
    if (!text.empty()) Audio::play2D(Audio::SFX_UI_TEXT, 0.4f);
#endif
}

void GameWorld::mTarget(vec2 p, UI::BlipIcon icon) {
    gHasTarget = true;
    gTarget = p;
    gTargetIcon = icon;
    missionTargetActive = true;
    missionTarget = p;
    gpsRecalcTimer = 0.f;
}

void GameWorld::mClearTarget() {
    gHasTarget = false;
    missionTargetActive = false;
    missionRoute.clear();
}

void GameWorld::mBlipPed(int ped, UI::BlipIcon icon) {
    TrackedBlip b;
    b.ped = ped;
    b.icon = icon;
    gTracked.push_back(b);
}

void GameWorld::mBlipVehicle(int veh, UI::BlipIcon icon) {
    TrackedBlip b;
    b.vehicle = veh;
    b.icon = icon;
    gTracked.push_back(b);
}

void GameWorld::mClearBlips() { gTracked.clear(); }

void GameWorld::mMarker(dvec3 pos, float radius, vec3 color) {
    Marker m;
    m.pos = pos;
    m.radius = radius;
    m.color = color;
    gMissions.markers.push_back(m);
}

void GameWorld::mClearMarkers() { gMissions.markers.clear(); }

void GameWorld::mSay(const DialogueLine& l) { gMissions.lines.push_back(l); }

void GameWorld::mSay(const std::string& speaker, const std::string& text, int ped, u32 color) {
    DialogueLine l;
    l.speaker = speaker;
    l.text = text;
    l.ped = ped;
    l.color = color;
    if (ped >= 0 && ped < (int)peds.size() && peds[ped].used) l.female = peds[ped].female;
    gMissions.lines.push_back(l);
}

bool GameWorld::mTalking() const { return !gMissions.lines.empty(); }

void GameWorld::mCutscene(const std::vector<CutsceneShot>& shots, bool skippable) {
    gMissions.shots = shots;
    gMissions.shotIndex = shots.empty() ? -1 : 0;
    gMissions.shotTime = 0.f;
    gMissions.skippable = skippable;
    if (!shots.empty()) {
        playerControl = false;
        hudVisible = false;
        rig.scriptActive = true;
        rig.cut = true;
    }
}

bool GameWorld::mInCutscene() const { return gMissions.shotIndex >= 0; }

bool GameWorld::playerAt(vec2 p, float r) const {
    if (player < 0 || !peds[player].used) return false;
    return length(peds[player].pos.toVec3().xy() - p) < r;
}

bool GameWorld::playerInVehicle(int veh) const { return player >= 0 && peds[player].used && peds[player].vehicle == veh && veh >= 0; }

void GameWorld::mEnd(bool passed, const std::string& reason) {
    MissionManager& M = gMissions;
    if (!M.active) return;
    const MissionDef& d = M.defs[M.activeDef];
    if (passed) {
        long long r = M.active->reward();
        pinfo.money += r;
        bigMessage("MISSION PASSED", r > 0 ? StrFormat("$%lld", r) : std::string(M.active->title()), 0xff33ccffu);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_MISSION_PASSED, 0.9f);
#endif
        if (d.setsFlag >= 0) {
            if ((int)storyFlags.size() <= d.setsFlag) storyFlags.resize(d.setsFlag + 1, 0);
            storyFlags[d.setsFlag] = 1;
        }
        if (d.storyIndex >= 0) storyTitle = M.active->title();
    } else {
        bigMessage("MISSION FAILED", reason.empty() ? M.active->failReason : reason, 0xff3030ffu);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_MISSION_FAILED, 0.9f);
#endif
    }
    // cleanup: entities go back to the ambient pool (not despawned while visible)
    for (int id : M.peds)
        if (id >= 0 && id < (int)peds.size() && peds[id].used && !peds[id].isPlayer) {
            peds[id].persistent = false;
            if (peds[id].health > 0.f && peds[id].faction == FAC_ENEMY) {
                peds[id].brain.type = BRAIN_FLEE;
                peds[id].brain.target = player;
            }
        }
    for (int id : M.vehicles)
        if (id >= 0 && id < (int)vehicles.size() && vehicles[id].used && playerVehicle() != id) vehicles[id].persistent = false;
    M.peds.clear();
    M.vehicles.clear();
    M.markers.clear();
    M.lines.clear();
    M.shots.clear();
    M.shotIndex = -1;
    gTracked.clear();
    mClearTarget();
    hudObjective.clear();
    missionTimerHud = -1.f;
    missionCounterLabel.clear();
    playerControl = true;
    hudVisible = true;
    rig.scriptActive = false;
    rig.cut = true;
    delete M.active;
    M.active = nullptr;
    M.activeDef = -1;
    M.cooldown = 6.f;
#ifdef HAVE_AUDIO
    Audio::setScore(0, 0.f);
#endif
}

void GameWorld::updateMissions(float dt) {
    MissionManager& M = gMissions;
    buildMarkerModel(renderer);
    M.cooldown = Max(0.f, M.cooldown - dt);
    Ped* pl = playerPed();
    // ---- dialogue playback
    if (!M.lines.empty()) {
        DialogueLine& l = M.lines.front();
        if (M.lineTimer <= 0.f && M.lineSound == 0) {
            Audio::VoiceParams v = (l.ped >= 0 && l.ped < (int)peds.size() && peds[l.ped].used) ? peds[l.ped].voice : Speech::presetVoice(l.female, l.voiceSeed);
            if (l.ped >= 0 && l.ped < (int)peds.size() && peds[l.ped].used)
                M.lineSound = Audio::speakAt(l.text.c_str(), v, pedHeadPos(peds[l.ped]), 1.f);
            else
                M.lineSound = Audio::speak(l.text.c_str(), v, 1.f);
            float dur = Audio::estimateSpeechDuration(l.text.c_str(), v);
            M.lineTimer = dur + l.pause;
            subtitle(l.speaker, l.text, dur + 0.3f, l.color);
            if (M.lineSound == 0) M.lineSound = 0xffffffffu;  // no audio device: time-based
        }
        M.lineTimer -= dt;
        bool done = M.lineTimer <= 0.f;
        if (done) {
            M.lines.erase(M.lines.begin());
            M.lineSound = 0;
            M.lineTimer = 0.f;
        }
    }
    // ---- cutscene camera
    if (M.shotIndex >= 0 && M.shotIndex < (int)M.shots.size()) {
        CutsceneShot& s = M.shots[M.shotIndex];
        M.shotTime += dt;
        float t = Saturate(M.shotTime / Max(s.duration, 0.01f));
        float e = t * t * (3.f - 2.f * t);
        rig.scriptPos = s.pos + rel(s.pos2, s.pos) * e;
        rig.scriptTarget = s.target + rel(s.target2, s.target) * e;
        if (length2(rel(s.pos2, dvec3(0, 0, 0))) < 1e-6f) rig.scriptPos = s.pos;
        if (length2(rel(s.target2, dvec3(0, 0, 0))) < 1e-6f) rig.scriptTarget = s.target;
        rig.scriptFov = s.fov;
        bool skip = M.skippable && ctl.skip.pressed && M.shotTime > 0.4f;
        if (M.shotTime >= s.duration || skip) {
            if (skip) {
                M.shotIndex = (int)M.shots.size();
                M.lines.clear();
                subTimer = 0.f;
            } else {
                M.shotIndex++;
            }
            M.shotTime = 0.f;
            if (M.shotIndex >= (int)M.shots.size()) {
                M.shotIndex = -1;
                M.shots.clear();
                playerControl = true;
                hudVisible = true;
                rig.scriptActive = false;
                rig.cut = true;
            }
        }
    }
    // ---- active mission
    if (M.active) {
        M.active->stageTime += dt;
        MissionStatus st = MS_RUNNING;
        if (!pl || pl->health <= 0.f) {
            st = MS_FAILED;
            M.active->failReason = "You died.";
        } else {
            st = M.active->update(*this, dt);
        }
        if (st == MS_PASSED) mEnd(true, "");
        else if (st == MS_FAILED) mEnd(false, M.active ? M.active->failReason : std::string());
    }
    // ---- blips for mission starts + tracked entities
    missionBlips.clear();
    if (M.active) {
        for (auto it = gTracked.begin(); it != gTracked.end();) {
            UI::Blip b;
            b.icon = it->icon;
            b.color = it->color;
            bool valid = false;
            if (it->ped >= 0 && it->ped < (int)peds.size() && peds[it->ped].used && peds[it->ped].health > 0.f) {
                b.pos = peds[it->ped].pos.toVec3().xy();
                b.heightDiff = (float)(peds[it->ped].pos.z - (pl ? pl->pos.z : 0.0));
                valid = true;
            } else if (it->vehicle >= 0 && it->vehicle < (int)vehicles.size() && vehicles[it->vehicle].used && !vehicles[it->vehicle].exploded) {
                b.pos = vehicles[it->vehicle].sim.body.pos.toVec3().xy();
                valid = true;
            }
            if (!valid) {
                it = gTracked.erase(it);
                continue;
            }
            missionBlips.push_back(b);
            ++it;
        }
        if (gHasTarget) {
            UI::Blip b;
            b.pos = gTarget;
            b.icon = gTargetIcon;
            b.flash = false;
            missionBlips.push_back(b);
        }
    } else {
        for (int i = 0; i < (int)M.defs.size(); i++) {
            const MissionDef& d = M.defs[i];
            if (!missionAvailable(i)) continue;
            UI::Blip b;
            b.pos = d.startPos;
            b.icon = d.icon;
            b.letter = d.letter;
            b.label = d.contact;
            b.shortRange = d.storyIndex < 0;
            missionBlips.push_back(b);
            // trigger
            if (pl && M.cooldown <= 0.f && playerControl) {
                vec2 pp = pl->pos.toVec3().xy();
                int pv = playerVehicle();
                float r = pv >= 0 ? 6.f : 2.5f;
                bool slow = pv < 0 || vehicles[pv].sim.speed() < 4.f;
                if (length(pp - d.startPos) < r && slow && pinfo.wanted == 0) {
                    startMission(i);
                    break;
                }
            }
        }
    }
    // ---- markers (glowing cylinders)
    for (const Marker& mk : M.markers) {
        if (!gMarkerModel) break;
        float d = pl ? length(rel(mk.pos, pl->pos)) : 1e9f;
        if (d > 400.f) continue;
        Render::DrawItem di;
        di.model = gMarkerModel;
        di.pos = mk.pos;
        di.scale = vec3(mk.radius, mk.radius, 1.6f + 0.15f * sinf((float)time * 3.f));
        di.tint0 = vec4(mk.color, 0.f);
        di.emissiveScale = 1.2f;
        di.castShadow = false;
        renderer->dynamic->submit(di);
    }
    // start markers for available missions (on foot scale)
    if (!M.active && gMarkerModel && pl) {
        for (int i = 0; i < (int)M.defs.size(); i++) {
            if (!missionAvailable(i)) continue;
            const MissionDef& d = M.defs[i];
            float dist = length(d.startPos - pl->pos.toVec3().xy());
            if (dist > 250.f) continue;
            Render::DrawItem di;
            di.model = gMarkerModel;
            float gz = groundHeight(d.startPos.x, d.startPos.y, (float)pl->pos.z + 5.f);
            di.pos = dvec3(d.startPos.x, d.startPos.y, gz);
            di.scale = vec3(1.1f, 1.1f, 1.2f);
            di.tint0 = vec4(d.storyIndex >= 0 ? vec3(1.f, 0.85f, 0.2f) : vec3(0.3f, 0.8f, 1.f), 0.f);
            di.castShadow = false;
            renderer->dynamic->submit(di);
        }
    }
}

bool GameWorld::missionAvailable(int i) const {
    const MissionDef& d = gMissions.defs[i];
    if (d.setsFlag >= 0 && d.setsFlag < (int)storyFlags.size() && storyFlags[d.setsFlag] && !d.repeatable) return false;
    if (d.requiresFlag >= 0 && (d.requiresFlag >= (int)storyFlags.size() || !storyFlags[d.requiresFlag])) return false;
    if (d.timeFrom != d.timeTo) {
        float t = env->timeOfDay;
        bool in = d.timeFrom < d.timeTo ? (t >= d.timeFrom && t < d.timeTo) : (t >= d.timeFrom || t < d.timeTo);
        if (!in) return false;
    }
    return true;
}

void GameWorld::startMission(int i) {
    MissionManager& M = gMissions;
    if (M.active || i < 0 || i >= (int)M.defs.size()) return;
    M.activeDef = i;
    M.active = M.defs[i].create();
    M.active->stage = 0;
    M.active->stageTime = 0.f;
    hasWaypoint = false;
    gpsRoute.clear();
    bigMessage(M.active->title(), M.defs[i].storyIndex >= 0 ? std::string(M.defs[i].contact) : std::string("Side activity"), 0xffffffffu);
    M.active->start(*this);
#ifdef HAVE_AUDIO
    Audio::setScore(hash32((u32)i) & 0xffff, 0.35f);
#endif
}

}  // namespace Game
