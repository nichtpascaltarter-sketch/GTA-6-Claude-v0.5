// Mission runtime: start triggers, per-frame script update, dialogue playback (TTS + subtitles), cutscene camera,
// objective markers, pass/fail flow, checkpoints with a retry prompt and cleanup of mission entities.
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
bool gRetryFading = false;

void buildMarkerModel(Render::Renderer* r) {
    if (gMarkerModel) return;
    MeshData m;
    const int seg = 40;
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
    gMarkerModel = r->dynamic->createModel(m);
}

bool classAvailable(const GameWorld& g, u32 mask) {
    // at least one of the classes in the mask must have a model in this build (boats, aircraft...)
    if (!mask) return true;
    for (const VehicleAsset& a : g.vassets)
        if (mask & (1u << (u32)a.spec.cls)) return true;
    return false;
}

}  // namespace mission_detail

using namespace mission_detail;

int MissionManager::findDef(const char* id) const {
    for (int i = 0; i < (int)defs.size(); i++)
        if (strcmp(defs[i].id, id) == 0) return i;
    return -1;
}

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
    gMissions.holdForDialogue = skippable;   // story cutscenes (the character switch camera is not skippable)
    gMissions.autoShots = 0;
    if (!shots.empty()) {
        playerControl = false;
        hudVisible = false;
        rig.scriptActive = true;
        rig.cut = true;
        // a vehicle the player was driving must not keep its last throttle input while the camera is scripted
        int pv = playerVehicle();
        if (pv >= 0 && peds[player].seat == 0) {
            vehicles[pv].ctl = Vehicles::VehicleControls();
            vehicles[pv].ctl.brake = 1.f;
            vehicles[pv].ctl.handbrake = true;
        }
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
    int defIndex = M.activeDef;
    M.active->finish(*this, passed);
    if (passed) {
        long long r = M.replay ? 0 : M.active->reward();
        pinfo.money += r;
        bigMessage(M.active->passBanner(), r > 0 ? StrFormat("$%lld", r) : std::string(M.active->title()), 0xff33ccffu);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_MISSION_PASSED, 0.9f);
#endif
        if (d.storyIndex >= 0 && !M.replay && player >= 0) socialReport(UI::TE_MISSION_PASSED, peds[player].pos, M.active->title());
        if (d.setsFlag >= 0 && !M.replay) {
            if ((int)storyFlags.size() <= d.setsFlag) storyFlags.resize(d.setsFlag + 1, 0);
            storyFlags[d.setsFlag] = 1;
        }
        if (d.storyIndex >= 0 && !M.replay) storyTitle = M.active->title();
        M.retry.def = -1;
        M.retry.timer = 0.f;
        M.retry.pending = false;
    } else {
        bigMessage(M.active->failBanner(), reason.empty() ? M.active->failReason : reason, 0xff3030ffu);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_MISSION_FAILED, 0.9f);
#endif
        // offer a retry from the last checkpoint once the player is back on their feet
        if (M.active->allowRetry()) {
            M.retry.def = defIndex;
            M.retry.checkpoint = M.checkpoint;
            M.retry.pending = true;
            M.retry.timer = 0.f;
            M.retry.replay = M.replay;
        } else {
            M.retry.def = -1;
            M.retry.pending = false;
        }
    }
    // cleanup: entities go back to the ambient pool (not despawned while visible)
    for (int id : M.peds)
        if (id >= 0 && id < (int)peds.size() && peds[id].used && !peds[id].isPlayer) {
            peds[id].persistent = false;
            peds[id].invincible = false;
            if (peds[id].health > 0.f && peds[id].faction == FAC_ENEMY) {
                peds[id].brain.type = BRAIN_FLEE;
                peds[id].brain.target = player;
            } else if (peds[id].health > 0.f && peds[id].state != PS_INVEHICLE &&
                       (peds[id].brain.type == BRAIN_NONE || peds[id].brain.type == BRAIN_FOLLOW || peds[id].brain.type == BRAIN_GOTO)) {
                peds[id].brain.type = BRAIN_WANDER;
                peds[id].brain.edge = -1;
                peds[id].animIn.stance = 0;
            } else if (peds[id].health > 0.f && peds[id].state == PS_INVEHICLE && peds[id].seat == 0 && peds[id].brain.type == BRAIN_NONE) {
                peds[id].brain.type = BRAIN_DRIVER;
            }
            // a buddy riding with the player gets out
            if (peds[id].vehicle >= 0 && peds[id].vehicle == playerVehicle()) removePedFromVehicle(id, false);
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
    timeScale = pinfo.deathTimer > 0.f ? timeScale : 1.f;
    policeSuppressed = false;
    M.suppressPolice = false;
    M.allowSwitch = false;
    delete M.active;
    M.active = nullptr;
    M.activeDef = -1;
    M.cooldown = 6.f;
    M.checkpoint = 0;
    M.replay = false;
#ifdef HAVE_AUDIO
    Audio::setScore(0, 0.f);
#endif
    openWorldOnMissionEnd(*this, defIndex, passed);
}

void GameWorld::updateMissions(float dt) {
    MissionManager& M = gMissions;
    buildMarkerModel(renderer);
    M.updateFrames++;
    M.cooldown = Max(0.f, M.cooldown - dt);
    if ((int)storyFlags.size() < kFlagCount) storyFlags.resize(kFlagCount, 0);
    Ped* pl = playerPed();
    // ---- dialogue playback
    if (!M.lines.empty()) {
        DialogueLine& l = M.lines.front();
        if (M.lineTimer <= 0.f && M.lineSound == 0) {
            bool pedVoice = l.ped >= 0 && l.ped < (int)peds.size() && peds[l.ped].used;
            Audio::VoiceParams v = l.hasVoice ? l.voice : (pedVoice ? peds[l.ped].voice : Speech::presetVoice(l.female, l.voiceSeed));
            std::string spoken = l.spoken.empty() ? speakableText(l.text) : l.spoken;
            if (pedVoice && !l.phone) {
                M.lineSound = Audio::speakAt(spoken.c_str(), v, pedHeadPos(peds[l.ped]), 1.f);
                startLipSync(l.ped, spoken.c_str(), v);
            } else
                M.lineSound = Audio::speak(spoken.c_str(), v, l.phone ? 0.85f : 1.f);
            float dur = Audio::estimateSpeechDuration(spoken.c_str(), v);
            if (M.test.active) dur = Min(dur, 1.0f);
            M.lineTimer = dur + (M.test.active ? 0.05f : l.pause);
            subtitle(l.speaker, Speech::displayText(l.text.c_str()), dur + 0.3f, l.color);
            if (pedVoice && !l.phone && peds[l.ped].state == PS_ONFOOT && peds[l.ped].brain.type == BRAIN_NONE && peds[l.ped].animIn.stance == 0 && !peds[l.ped].isPlayer)
                peds[l.ped].animIn.stance = 7;
            // blocking: in a cutscene, the people standing around turn to whoever speaks
            if (pedVoice && !l.phone && M.shotIndex >= 0) {
                vec3 sp = peds[l.ped].pos.toVec3();
                auto turn = [&](int id) {
                    if (id < 0 || id == l.ped || id >= (int)peds.size() || !peds[id].used || peds[id].health <= 0.f) return;
                    Ped& q = peds[id];
                    if (q.state != PS_ONFOOT || (!q.isPlayer && q.brain.type != BRAIN_NONE)) return;
                    vec2 d = sp.xy() - q.pos.toVec3().xy();
                    float dist = length(d);
                    if (dist < 0.6f || dist > 9.f) return;
                    q.yaw = atan2f(-d.x, d.y);
                };
                for (int id : M.peds) turn(id);
                turn(player);
            }
            if (M.lineSound == 0) M.lineSound = 0xffffffffu;  // no audio device: time-based
        }
        M.lineTimer -= dt;
        bool done = M.lineTimer <= 0.f;
        if (done) {
            DialogueLine& fl = M.lines.front();
            if (fl.ped >= 0 && fl.ped < (int)peds.size() && peds[fl.ped].used && peds[fl.ped].animIn.stance == 7 && peds[fl.ped].brain.type == BRAIN_NONE)
                peds[fl.ped].animIn.stance = 0;
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
            // scripted shots are used up but the conversation goes on: frame the current speaker
            if (!skip && M.shotIndex >= (int)M.shots.size() && M.holdForDialogue && !M.lines.empty() && M.autoShots < 24) {
                CutsceneShot sh;
                if (speakerShot(M.lines.front().ped, M.shots.empty() ? nullptr : &M.shots.back(), M.lineTimer, sh)) {
                    M.shots.push_back(sh);
                    M.autoShots++;
                }
            }
            if (M.shotIndex >= (int)M.shots.size()) {
                M.shotIndex = -1;
                M.shots.clear();
                playerControl = true;
                hudVisible = true;
                rig.scriptActive = false;
                rig.cut = true;
            }
        } else if (s.speaker != -2 && !M.lines.empty() && M.lines.front().ped != s.speaker && M.shotTime > 0.6f) {
            // a runtime speaker shot: cut to the next speaker as soon as they talk
            CutsceneShot sh;
            if (speakerShot(M.lines.front().ped, &s, M.lineTimer, sh)) {
                M.shots.push_back(sh);
                M.shots.erase(M.shots.begin() + M.shotIndex);
                M.shotIndex = (int)M.shots.size() - 1;
                M.shotTime = 0.f;
                M.autoShots++;
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
        } else if (pinfo.busted) {
            st = MS_FAILED;
            M.active->failReason = "You got busted.";
        } else {
            M.active->preUpdate(*this, dt);
            st = M.active->update(*this, dt);
        }
        if (st == MS_PASSED) mEnd(true, "");
        else if (st == MS_FAILED) mEnd(false, M.active ? M.active->failReason : std::string());
    }
    // police stay out of missions that ask for it (heat is held below the one-star threshold)
    policeSuppressed = M.active && M.suppressPolice;
    if (policeSuppressed) {
        pinfo.wanted = 0;
        pinfo.wantedHeat = -1000.f;
        pinfo.wantedCooldown = 0.f;
    } else if (pinfo.wantedHeat < 0.f) {
        pinfo.wantedHeat = 0.f;
    }
    // ---- blips for mission starts + tracked entities
    missionBlips.clear();
    if (M.active) {
        for (auto it = gTracked.begin(); it != gTracked.end();) {
            UI::Blip b;
            b.icon = it->icon;
            b.color = it->color ? it->color : 0xffffffffu;
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
            if (d.hidden || !missionAvailable(i)) continue;
            bool mine = d.protagonist < 0 || d.protagonist == protagonistIndex;
            UI::Blip b;
            b.pos = d.startPos;
            b.icon = d.icon;
            b.letter = d.letter;
            b.label = d.contact;
            b.shortRange = d.storyIndex < 0;
            b.scale = mine ? 1.f : 0.75f;
            if (!mine) b.color = d.protagonist == 0 ? 0xffb080ffu : 0xffffc060u;   // the other protagonist's missions
            missionBlips.push_back(b);
            // trigger
            if (pl && pl->health > 0.f && pinfo.deathTimer <= 0.f && M.cooldown <= 0.f && playerControl && !openWorldBusy() && !M.retry.pending &&
                M.retry.timer <= 0.f) {
                vec2 pp = pl->pos.toVec3().xy();
                int pv = playerVehicle();
                float r = pv >= 0 ? 6.f : 2.5f;
                bool slow = pv < 0 || vehicles[pv].sim.speed() < 4.f;
                if (length(pp - d.startPos) < r && slow && pinfo.wanted == 0) {
                    if (!mine) {
                        if (hudHelpTimer <= 0.f)
                            help(StrFormat("Only %s can start this mission. Open the phone (~i:UP|UP~) to switch characters.",
                                           d.protagonist == 0 ? "Mari" : "Dex"),
                                 3.f);
                        continue;
                    }
                    M.startCheckpoint = 0;
                    startMission(i);
                    break;
                }
            }
        }
    }
    // ---- retry prompt after a failed mission
    if (!M.active && M.retry.def >= 0) {
        bool alive = pl && pl->health > 0.f && pinfo.deathTimer <= 0.f && !pinfo.busted;
        if (M.retry.pending && alive) {
            M.retry.pending = false;
            M.retry.timer = 14.f;
        }
        if (!M.retry.pending && M.retry.timer > 0.f) {
            M.retry.timer -= dt;
            if (!gRetryFading) {
                const MissionDef& rd = M.defs[M.retry.def];
                help(StrFormat("~r~%s~s~ failed.~n~~i:ENTER|A~ Retry%s   ~i:BACKSPACE|B~ Quit", rd.title[0] ? rd.title : rd.id,
                               M.retry.checkpoint > 0 ? " from checkpoint" : ""),
                     0.25f);
                if (ctl.confirm.pressed && playerControl) {
                    gRetryFading = true;
                    fadeOut(3.f);
                } else if (ctl.back.pressed || M.retry.timer <= 0.f) {
                    M.retry.def = -1;
                    M.retry.timer = 0.f;
                    hudHelpTimer = 0.f;
                }
            } else if (fadedOut()) {
                // restore the loadout the mission started with and restart at the checkpoint
                if (pl) {
                    for (int w = 0; w < WPN_COUNT; w++) {
                        if (M.retry.hasWeapon[w]) {
                            pl->hasWeapon[w] = true;
                            pl->ammo[w] = Max(pl->ammo[w], M.retry.ammo[w]);
                            if (pl->clip[w] <= 0) pl->clip[w] = Min(weaponInfo((WeaponType)w).clipSize, pl->ammo[w]);
                        }
                    }
                    pl->health = pl->maxHealth;
                    pl->armor = Max(pl->armor, M.retry.armor);
                    pl->weapon = (WeaponType)Clamp(M.retry.weapon, 0, (int)WPN_COUNT - 1);
                    if (!pl->hasWeapon[pl->weapon]) pl->weapon = WPN_FISTS;
                }
                pinfo.wanted = 0;
                pinfo.wantedHeat = 0.f;
                int def = M.retry.def;
                M.startCheckpoint = M.retry.checkpoint;
                M.replay = M.retry.replay;
                M.retry.def = -1;
                M.retry.timer = 0.f;
                gRetryFading = false;
                hudHelpTimer = 0.f;
                M.cooldown = 0.f;
                startMission(def);
                fadeIn(1.5f);
            }
        }
    } else if (M.active) {
        gRetryFading = false;
    }
    // ---- markers (glowing cylinders)
    drawMarkers(*this, M.markers);
    // start markers for available missions (on foot scale)
    if (!M.active && gMarkerModel && pl) {
        for (int i = 0; i < (int)M.defs.size(); i++) {
            if (M.defs[i].hidden || !missionAvailable(i)) continue;
            const MissionDef& d = M.defs[i];
            float dist = length(d.startPos - pl->pos.toVec3().xy());
            if (dist > 250.f) continue;
            bool mine = d.protagonist < 0 || d.protagonist == protagonistIndex;
            Render::DrawItem di;
            di.model = gMarkerModel;
            float gz = groundHeight(d.startPos.x, d.startPos.y, (float)pl->pos.z + 5.f);
            di.pos = dvec3(d.startPos.x, d.startPos.y, gz);
            di.scale = vec3(1.1f, 1.1f, 1.2f);
            vec3 col = d.storyIndex >= 0 ? vec3(1.f, 0.85f, 0.2f) : vec3(0.3f, 0.8f, 1.f);
            if (!mine) col = d.protagonist == 0 ? vec3(1.f, 0.45f, 0.7f) : vec3(0.35f, 0.75f, 1.f);
            di.tint0 = vec4(col, 0.f);
            di.castShadow = false;
            renderer->dynamic->submit(di);
        }
    }
    // ---- open world: activities, shops, economy, phone, switching, test automation
    updateOpenWorld(*this, dt);
}

void drawMarkers(GameWorld& g, const std::vector<Marker>& list) {
    Ped* pl = g.playerPed();
    for (const Marker& mk : list) {
        if (!gMarkerModel) break;
        float d = pl ? length(rel(mk.pos, pl->pos)) : 1e9f;
        if (d > 400.f) continue;
        Render::DrawItem di;
        di.model = gMarkerModel;
        di.pos = mk.pos;
        di.scale = vec3(mk.radius, mk.radius, 1.6f + 0.15f * sinf((float)g.time * 3.f));
        di.tint0 = vec4(mk.color, 0.f);
        di.emissiveScale = 1.2f;
        di.castShadow = false;
        g.renderer->dynamic->submit(di);
    }
}

bool GameWorld::missionAvailable(int i) const {
    const MissionDef& d = gMissions.defs[i];
    if (d.setsFlag >= 0 && d.setsFlag < (int)storyFlags.size() && storyFlags[d.setsFlag] && !d.repeatable) return false;
    if (d.requiresFlag >= 0 && (d.requiresFlag >= (int)storyFlags.size() || !storyFlags[d.requiresFlag])) return false;
    if (d.requiresFlag2 >= 0 && (d.requiresFlag2 >= (int)storyFlags.size() || !storyFlags[d.requiresFlag2])) return false;
    if (!classAvailable(*this, d.needsClasses)) return false;
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
    int cp = M.startCheckpoint;
    M.startCheckpoint = 0;
    Ped* pl = playerPed();
    if (cp == 0 && pl) {
        // loadout snapshot for retries
        for (int w = 0; w < WPN_COUNT; w++) {
            M.retry.hasWeapon[w] = pl->hasWeapon[w];
            M.retry.ammo[w] = pl->ammo[w];
        }
        M.retry.armor = pl->armor;
        M.retry.weapon = (int)pl->weapon;
        M.retry.money = pinfo.money;
    }
    M.activeDef = i;
    M.active = M.defs[i].create();
    M.active->stage = 0;
    M.active->stageTime = 0.f;
    M.active->checkpoint = cp;
    M.checkpoint = cp;
    M.suppressPolice = false;
    M.allowSwitch = false;
    M.lines.clear();
    hasWaypoint = false;
    gpsRoute.clear();
    hudHelpTimer = 0.f;
    bigMessage(M.active->title(), M.defs[i].storyIndex >= 0 ? std::string(M.defs[i].contact) : std::string("Side activity"), 0xffffffffu);
#ifdef HAVE_AUDIO
    Audio::setScore(hash32((u32)i) & 0xffff, 0.35f);   // missions usually pick their own score in start()
#endif
    M.active->start(*this);
}

}  // namespace Game
