// GameWorld lifecycle, the per-frame simulation step and render submission of all dynamic entities.
#include "gameworld.h"

namespace Game {

namespace gw_detail {

mat3 frameFromForward(vec3 fwd, vec3 upHint) {
    vec3 y = normalize(fwd);
    vec3 x = cross(y, upHint);
    if (length2(x) < 1e-6f) x = cross(y, vec3(1, 0, 0));
    x = normalize(x);
    vec3 z = cross(x, y);
    return mat3(x, y, z);
}

}  // namespace gw_detail

using namespace gw_detail;

void GameWorld::init(Render::Renderer* r, Render::Environment* e, World::WorldMap* m, World::RoadNetwork* rn, World::BuildingSet* b) {
    renderer = r;
    env = e;
    map = m;
    roads = rn;
    buildings = b;
    gGame = this;
    peds.reserve(256);
    vehicles.reserve(160);
    buildAssets();
    gMissions.registerAll(*this);
    initAI();   // lane/sidewalk graphs now (1-2.5 s on the loading thread) instead of a hitch on the first frame
}

void GameWorld::shutdown() {
    for (int i = 0; i < (int)vehicles.size(); i++)
        if (vehicles[i].used) despawnVehicle(i, true);
    for (int i = 0; i < (int)peds.size(); i++)
        if (peds[i].used) despawnPed(i);
    for (auto& f : fires) {
#ifdef HAVE_AUDIO
        if (f.snd) Audio::destroyEmitter(f.snd);
#endif
        f.used = false;
    }
}

void GameWorld::resetWorldForLoad() {
    for (int i = 0; i < (int)vehicles.size(); i++)
        if (vehicles[i].used) despawnVehicle(i, true);
    for (int i = 0; i < (int)peds.size(); i++)
        if (peds[i].used) despawnPed(i);
    for (auto& f : fires) {
#ifdef HAVE_AUDIO
        if (f.snd) Audio::destroyEmitter(f.snd);
#endif
        f.used = false;
    }
    projectiles.clear();
    pickups.clear();
    crimes.clear();
    player = -1;
    missionBlips.clear();
    missionRoute.clear();
    missionTargetActive = false;
    hasWaypoint = false;
    gpsRoute.clear();
    hudObjective.clear();
    hudHelpTimer = hudNoteTimer = subTimer = 0.f;
    hudBigTime = -1.f;
    timeScale = 1.f;
    playerControl = true;
    rig = CameraRig();
    rig.cut = true;
    placeWorldPickups();
}

void GameWorld::spawnPlayer(dvec3 pos, float yaw) {
    int ci = protagonistChar[Clamp(protagonistIndex, 0, 1)];
    if (ci < 0) ci = chars.empty() ? -1 : 0;
    if (ci < 0) return;
    player = spawnPed(ci, pos, yaw, FAC_PLAYER);
    if (player < 0) return;
    Ped& p = peds[player];
    p.isPlayer = true;
    p.persistent = true;
    p.maxHealth = 200.f;
    p.health = 200.f;
    p.hasWeapon[WPN_FISTS] = true;
    p.voice = Speech::presetVoice(protagonistIndex == 0, protagonistIndex == 0 ? 7u : 11u);
    rig = CameraRig();
    rig.yaw = yaw;
    rig.cut = true;
}

void GameWorld::newGame() {
    pinfo = PlayerInfo();
    pinfo.money = 3500;
    storyFlags.assign(128, 0);
    ownedVehicleModels.clear();
    storyTitle = "Prologue";
    gameDay = 1;
    env->timeOfDay = 17.2f;
    resetWorldForLoad();
    // Start: Mari's street in Calle Luna (district box x 0.95-2.62 km, y -0.92..0.92 km)
    vec2 start(1780.f, 160.f);
    float s = 0, side = 0;
    int e = roads->nearestEdge(start, 400.f, &s, nullptr, &side);
    vec3 pos(start, 0.f);
    float yaw = 0.f;
    if (e >= 0) {
        const World::RoadEdge& ed = roads->edges[e];
        vec3 c = ed.posAt(s);
        vec3 t = ed.tangentAt(s);
        vec3 n = normalize(vec3(-t.y, t.x, 0)) * (side >= 0 ? 1.f : -1.f);
        pos = c + n * (ed.halfWidth + Max(ed.sidewalk, 1.2f) * 0.5f);
        yaw = atan2f(-t.x, t.y);
    }
    pos.z = groundHeight(pos.x, pos.y, pos.z + 30.f);
    spawnPlayer(dvec3(pos), yaw);
    fadeAlpha = 1.f;
    fadeIn(0.6f);
    populationWarmup = 2.5f;
    giveWeapon(player, WPN_PISTOL, 60);
    // a parked starter car next to the player
    int model = findVehicleModel(Vehicles::VC_COUPE, 3);
    if (model < 0) model = findVehicleModel(Vehicles::VC_SEDAN, 1);
    if (model >= 0 && e >= 0) {
        const World::RoadEdge& ed = roads->edges[e];
        float s2 = Clamp(s + 7.f, ed.cut0 + 3.f, ed.length - ed.cut1 - 3.f);
        vec3 c = ed.posAt(s2);
        vec3 t = ed.tangentAt(s2) * (side >= 0 ? 1.f : -1.f);
        const World::RoadClassInfo& info = World::roadInfo(ed.cls);
        vec3 n = normalize(vec3(t.y, -t.x, 0));
        vec3 cp = c + n * (ed.halfWidth - Max(info.shoulder, 1.2f) * 0.5f - 0.6f);
        int vid = spawnVehicle(model, dvec3(cp.x, cp.y, cp.z + 0.3f), atan2f(-t.x, t.y), false);
        if (vid >= 0) {
            vehicles[vid].persistent = true;
            vehicles[vid].playerUsed = true;
            vehicles[vid].parked = true;
            vehicles[vid].color0 = vec3(0.55f, 0.06f, 0.2f);
            vehicles[vid].color1 = vec3(0.05f);
            vehicles[vid].sim.engineOn = false;
        }
    }
}

void GameWorld::update(float realDt) {
    float dt = Min(realDt, 0.05f) * timeScale;
    dtLast = dt;
    if (paused) return;
    float fr = Min(realDt, 0.05f);
    if (fadeAlpha < fadeTarget) fadeAlpha = Min(fadeTarget, fadeAlpha + fr * fadeSpeed);
    else if (fadeAlpha > fadeTarget) fadeAlpha = Max(fadeTarget, fadeAlpha - fr * fadeSpeed);
    letterbox = Saturate(letterbox + (mInCutscene() ? fr : -fr) * 2.5f);
    time += dt;
    pinfo.playTime += realDt;
    hudRadioTimer = Max(0.f, hudRadioTimer - realDt);
    Phys::gWaves.time = env->gameSeconds;
    Phys::gWaves.windDir = env->windDir;
    Phys::gWaves.strength = env->wind;
    populationWarmup = Max(0.f, populationWarmup - realDt);
    if (pinfo.deathTimer <= 0.f) updateFocus(Min(realDt, 0.05f));
    updateTutorialHints(realDt);
    double t0 = TimeSeconds();
    Interiors::preUpdate(*this, dt);   // interiors_game.cpp (test walk-through controls)
    Transit::update(*this, dt);        // transit_game.cpp: trains, buses, ferries; handles boarding input before the player
    updatePlayer(dt);
    double t1 = TimeSeconds();
    updateAI(dt);
    updateAmbientTraffic(dt);
    updatePublicAddress(dt);
    double t2 = TimeSeconds();
    Vehicles::setSurfaceWetness(env->wetness);   // wet roads: longer stops, less grip at speed
    updateVehicles(dt);
    double t3 = TimeSeconds();
    updatePeds(dt);
    Interiors::update(*this, dt);      // interiors_game.cpp: streaming, collision, doors, NPCs, player inside
    double t4 = TimeSeconds();
    updateProjectiles(dt);
    updateFires(dt);
    updatePickups(dt);
    updateWanted(dt);
    updateSocial(realDt);
    double t5 = TimeSeconds();
    updateMissions(dt);
    updateGps(realDt);
    double t6 = TimeSeconds();
    sanitizeEntities();
    updateCamera(realDt);
    updateFirstPersonWeapon(realDt);   // after the camera: the gun is placed in front of the eyes
    Wildlife::update(*this, dt);   // animals around the player (wildlife.cpp; after the camera: LOD / spawning use it)
    updateRumble(realDt);
    updatePostFx(realDt);
    updateAutosave(realDt);
    double t7 = TimeSeconds();
    auto ema = [](float& v, double ms) { v = Lerp(v, (float)ms, 0.1f); };
    ema(profPlayer, (t1 - t0) * 1000.0);
    ema(profAI, (t2 - t1) * 1000.0);
    ema(profVehicles, (t3 - t2) * 1000.0);
    ema(profPeds, (t4 - t3) * 1000.0);
    ema(profMisc, (t5 - t4) * 1000.0);
    ema(profMissions, (t6 - t5) * 1000.0);
    ema(profCamera, (t7 - t6) * 1000.0);
}

// Test render (--weaponshowcase x,y,z): a gun rack for checking attachment placement and tints.
void GameWorld::submitWeaponShowcase() {
    Render::DynamicRenderer* dyn = renderer->dynamic;
    // gun rack for checking attachment placement: one gun per column along +X (pistol .. RPG), stock in the lower
    // row, every component fitted in the upper row with the column's tint; barrels point +X (view it looking north)
    int col = 0;
    for (int w = WPN_PISTOL; w <= WPN_RPG; w++, col++) {
        for (int row = 0; row < 2; row++) {
            int tint = row ? (col % (kWeaponTints - 1)) + 1 : 0;
            Render::DrawItem d;
            d.model = weaponTintModels[w][tint] ? weaponTintModels[w][tint] : weaponModels[w];
            if (!d.model) continue;
            d.pos = showcasePos + dvec3(col * 1.15 - 3.45, 0.0, row * 0.45);   // side-on to a camera looking north
            d.rot = frameFromForward(vec3(1, 0, 0), vec3(0, 0, 1));
            d.id = 0x6f0000000ull + (u64)(w * 2 + row);
            dyn->submit(d);
            u8 comps = row ? weaponCompsAvailable((WeaponType)w) : 0;
            for (int c = 0; c < kWeaponCompCount; c++) {
                if (!(comps & (1 << c)) || !weaponCompModels[w][c]) continue;
                Render::DrawItem cd = d;
                cd.model = weaponCompModels[w][c];
                cd.id = d.id + ((u64)(c + 1) << 40);
                dyn->submit(cd);
            }
        }
    }
}

// Autosave into the last slot once things are calm: after a mission is passed (never mid-mission, wanted, dead,
// in a cutscene or a fade) and every 10 minutes of free roam.
void GameWorld::updateAutosave(float realDt) {
    if (!autosaveEnabled) return;
    autosaveTimer += realDt;
    const Ped* pl = playerPed();
    bool calm = pl && pl->health > 0.f && pl->state != PS_RAGDOLL && pl->state != PS_GETUP && !missionActive() && pinfo.wanted == 0 &&
                playerControl && fadeAlpha < 0.01f && !rig.scriptActive;
    if (!calm || (!autosaveRequested && autosaveTimer < 600.f)) return;
    autosaveRequested = false;
    autosaveTimer = 0.f;
    if (saveGame(7, "Autosave - " + storyTitle)) notify("AUTOSAVE", "Progress saved");
}

// Guards against numerical blow-ups or entities escaping the world: non-finite or far-out-of-bounds entities are
// reset (player) or removed (everyone else) instead of propagating NaNs into rendering/physics.
void GameWorld::sanitizeEntities() {
    auto bad = [](dvec3 p) {
        return !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || fabs(p.x) > 30000.0 || fabs(p.y) > 30000.0 || p.z < -500.0 ||
               p.z > 6000.0;
    };
    for (int i = 0; i < (int)vehicles.size(); i++) {
        Vehicle& v = vehicles[i];
        if (!v.used) continue;
        bool nanVel = !std::isfinite(v.sim.body.vel.x) || !std::isfinite(v.sim.body.vel.y) || !std::isfinite(v.sim.body.vel.z);
        if (!bad(v.sim.body.pos) && !nanVel) continue;
        LOG("sanitize: vehicle %d (model %d) invalid state, removing", i, v.model);
        bool hasPlayer = false;
        for (int s = 0; s < 8; s++)
            if (v.seats[s] >= 0 && peds[v.seats[s]].isPlayer) hasPlayer = true;
        if (hasPlayer) {
            Ped& p = peds[player];
            removePedFromVehicle(player, false);
            GameWorld_respawnPlayer(*this);
            (void)p;
        }
        despawnVehicle(i, true);
    }
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!p.used) continue;
        bool nanVel = !std::isfinite(p.vel.x) || !std::isfinite(p.vel.y) || !std::isfinite(p.vel.z);
        if (!bad(p.pos) && !nanVel && std::isfinite(p.yaw)) continue;
        LOG("sanitize: ped %d invalid state", i);
        if (p.isPlayer) {
            p.vel = vec3(0);
            p.yaw = 0.f;
            freeRagdoll(p.ragdoll);
            p.state = PS_ONFOOT;
            GameWorld_respawnPlayer(*this);
        } else {
            despawnPed(i);
        }
    }
}

void GameWorld::updatePostFx(float realDt) {
    Render::PostFxControls fx;
    Ped* pl = playerPed();
    float dt = Min(realDt, 0.05f);
    fxDamage = Max(0.f, fxDamage - dt * 1.6f);
    fxFlash = Max(0.f, fxFlash - dt * 3.f);
    fxChroma = Max(0.f, fxChroma - dt * 2.5f);
    if (pl) {
        float hp = Saturate(pl->health / pl->maxHealth);
        // low health: pulsing red vignette with a heartbeat rhythm, colors drain
        if (hp < 0.3f && pl->health > 0.f) {
            float beat = powf(Saturate(sinf((float)time * 7.5f)), 8.f);
            float k = (0.3f - hp) / 0.3f;
            fx.vignette = Max(fx.vignette, 0.2f + 0.25f * k + beat * 0.15f);
            fx.vignetteColor = vec3(0.45f, 0.02f, 0.02f);
            fx.saturation = Lerp(1.f, 0.55f, k);
        }
        if (fxDamage > 0.f) {
            fx.vignette = Max(fx.vignette, fxDamage * 0.45f);
            fx.vignetteColor = vec3(0.5f, 0.02f, 0.02f);
        }
        if (pinfo.focusActive) {
            fx.saturation = Min(fx.saturation, 0.8f);
            fx.tint = protagonistIndex == 0 ? vec3(1.04f, 0.98f, 1.03f) : vec3(0.97f, 1.0f, 1.05f);
            fx.vignette = Max(fx.vignette, 0.22f);
            fx.chromatic = Max(fx.chromatic, 0.08f);
        }
        if (pinfo.weaponWheel) {
            fx.blur = 0.45f;
            fx.saturation = Min(fx.saturation, 0.6f);
        }
        // death / arrest screens
        if (pinfo.deathTimer > 0.f) {
            float k = Saturate(pinfo.deathTimer / 1.2f);
            fx.saturation = Lerp(fx.saturation, pinfo.busted ? 0.5f : 0.12f, k);
            fx.blur = Max(fx.blur, 0.3f * k);
            fx.vignette = Max(fx.vignette, 0.55f * k);
            fx.vignetteColor = pinfo.busted ? vec3(0.02f, 0.03f, 0.08f) : vec3(0.25f, 0.01f, 0.01f);
            fx.grain = 0.2f * k;
        }
    }
    // camera under the water surface
    float wz;
    vec3 cp = rig.cam.pos.toVec3();
    if (Phys::waterSurface(cp.x, cp.y, wz) && cp.z < wz - 0.05f) fx.underwater = 1.f;
    fx.flash = fxFlash;
    fx.flashColor = fxFlashColor;
    fx.chromatic = Max(fx.chromatic, fxChroma);
    renderer->postFx = fx;
}

void GameWorld::updateRumble(float dt) {
    float low = rumbleLow, high = rumbleHigh;
    // continuous engine/road vibration while driving
    int pv = playerVehicle();
    if (pv >= 0) {
        const Vehicles::VehicleState& s = vehicles[pv].sim;
        float rpm = Saturate((s.engineRpm - 800.f) / 6000.f);
        low = Max(low, s.engineOn ? 0.04f + rpm * 0.06f : 0.f);
        float slip = 0.f;
        for (int w = 0; w < s.wheelCount; w++)
            if (s.wheels[w].contact) slip = Max(slip, s.wheels[w].slip);
        high = Max(high, Saturate(slip - 0.3f) * 0.35f);
        if (s.wheelsOnGround > 0 && s.speed() > 5.f) {
            for (int w = 0; w < s.wheelCount; w++)
                if (s.wheels[w].contact && s.wheels[w].surface != Phys::SURF_ASPHALT && s.wheels[w].surface != Phys::SURF_CONCRETE) {
                    low = Max(low, 0.12f);
                    break;
                }
        }
    }
    if (paused || !vibration) low = high = 0.f;
    Platform::setGamepadRumble(Saturate(low), Saturate(high));
    rumbleLow = Max(0.f, rumbleLow - dt * 3.f);
    rumbleHigh = Max(0.f, rumbleHigh - dt * 5.f);
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::submitRender() {
    Render::DynamicRenderer* dyn = renderer->dynamic;
    dvec3 cam = rig.cam.pos;
    vec3 camF = rig.cam.forward();
    bool night = env->timeOfDay < 6.8f || env->timeOfDay > 19.2f;
    Interiors::submit(*this);   // enterable interiors: rooms, doors, lights, ambient volumes
    // ---- vehicles
    struct LitCar {
        float d;
        int id;
    };
    std::vector<LitCar> lit;
    for (int vi = 0; vi < (int)vehicles.size(); vi++) {
        Vehicle& v = vehicles[vi];
        if (!v.used) continue;
        const VehicleAsset& a = vassets[v.model];
        const Vehicles::VehicleState& s = v.sim;
        float dist = v.visibleDist;
        if (dist > (v.renderFar ? 9000.f : 1600.f)) continue;
        vec3 toV = rel(s.body.pos, cam);
        float rad = length(a.spec.boxHalf) + 1.f;
        if (dot(toV, camF) < -rad && dist > rad) continue;  // behind the camera
        mat3 R = s.body.rotMat();
        u32 bits = 0;
        bool driven = v.seats[0] >= 0;
        if (v.lightsOn) bits |= 1u;
        if (driven && (v.ctl.brake > 0.1f && s.forwardSpeed() > 0.5f)) bits |= 2u;
        if (driven && s.gear < 0) bits |= 4u;
        if (v.indicator < 0 || v.indicator == 2 || v.alarm) bits |= 8u;    // 2 = hazard lights (both sides)
        if (v.indicator == 1 || v.indicator == 2 || v.alarm) bits |= 16u;
        if (v.sirenOn) bits |= 32u;
        // level of detail by distance (the player's own vehicle always full detail)
        int vlod = vi == playerVehicle() ? 0 : (dist < 40.f ? 0 : (dist < 120.f ? 1 : 2));
        if (vlod > 0 && !a.bodyLod[vlod - 1]) vlod = 0;
        Render::DrawItem d;
        d.model = vlod == 0 ? a.body : a.bodyLod[vlod - 1];
        d.pos = s.body.pos;
        d.rot = R;
        if (s.visPitch != 0.f || s.visRoll != 0.f || s.visHeave != 0.f) {
            // exaggerated visual weight transfer (vehicle sim): rotate the body about the pivot, wheels stay put
            mat3 B = mat3FromQuat(quatAxisAngle(vec3(1, 0, 0), s.visPitch)) * mat3FromQuat(quatAxisAngle(vec3(0, 1, 0), s.visRoll));
            vec3 off = s.visPivot - B * s.visPivot + vec3(0.f, 0.f, s.visHeave);
            d.rot = R * B;
            d.pos = s.body.pos + dvec3(R * off);
        }
        d.tint0 = vec4(v.color0, v.dirt);
        d.tint1 = vec4(v.color1, 0.f);
        d.lightBits = bits;
        d.id = 0x100000000ull | v.uid;
        d.castShadow = dist < 350.f;
        d.damage0 = vec4(s.damageZones[0], s.damageZones[1], s.damageZones[2], s.damageZones[3]);
        d.damage1 = vec4(s.damageZones[4], s.damageZones[5], 0.f, 0.f);
        d.dmgBoxC = vec4(a.spec.boxCenter, 0.f);
        d.dmgBoxH = vec4(a.spec.boxHalf, 1.f);
        d.drawGlass = !v.windowsBroken;
        d.paintFinish = (float)v.mods.finish;
        d.glassTint = v.mods.tint * 0.3f;
        dyn->submit(d);
        // neon underglow: coloured light pools under the body
        if (length2(v.mods.neon) > 1e-4f && dist < 180.f) {
            for (int k = -1; k <= 1; k += 2) {
                Render::DynamicLight nl;
                nl.pos = s.body.pos + dvec3(R * vec3(0.f, a.spec.boxCenter.y + a.spec.boxHalf.y * 0.45f * (float)k, a.spec.boxCenter.z - a.spec.boxHalf.z + 0.12f));
                nl.color = v.mods.neon * 60.f;
                nl.radius = 3.2f + a.spec.boxHalf.x;
                renderer->addLight(nl);
            }
        }
        if (a.wheel && dist < 400.f && vlod < 2) {   // LOD2 shells carry their wheels
            for (int w = 0; w < s.wheelCount; w++) {
                vec3 lp;
                quat lq;
                Vehicles::wheelLocalTransform(s, w, lp, lq);
                Render::DrawItem wd;
                wd.model = vlod == 1 && a.wheelLod1 ? a.wheelLod1 : a.wheel;
                wd.pos = s.body.pos + R * lp;
                mat3 wr = R * mat3FromQuat(lq);
                if (a.spec.wheels[w].left) wr = wr * mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), kPi));
                wd.rot = wr;
                wd.tint0 = d.tint0;
                wd.tint1 = d.tint1;
                wd.id = 0x200000000ull | ((u64)v.uid << 4) | (u64)w;
                wd.castShadow = dist < 120.f;
                dyn->submit(wd);
            }
        }
        if (a.rotor) {
            Render::DrawItem rd;
            rd.model = a.rotor;
            bool heli = a.spec.cls == Vehicles::VC_HELI;
            quat spin = heli ? quatAxisAngle(vec3(0, 0, 1), s.rotorAngle) : quatAxisAngle(vec3(0, 1, 0), s.rotorAngle);
            rd.pos = s.body.pos + R * a.spec.rotorPos;
            rd.rot = R * mat3FromQuat(spin);
            rd.tint0 = d.tint0;
            rd.tint1 = d.tint1;
            rd.id = 0x300000000ull | v.uid;
            dyn->submit(rd);
        }
        if (a.tailRotor) {
            Render::DrawItem rd;
            rd.model = a.tailRotor;
            rd.pos = s.body.pos + R * a.spec.tailRotorPos;
            rd.rot = R * mat3FromQuat(quatAxisAngle(vec3(1, 0, 0), s.tailRotorAngle));
            rd.tint0 = d.tint0;
            rd.id = 0x400000000ull | v.uid;
            dyn->submit(rd);
        }
        if ((v.lightsOn || v.sirenOn || (bits & 2u)) && dist < 260.f) lit.push_back({dist, vi});
    }
    // Real light sources for the nearest lit vehicles
    std::sort(lit.begin(), lit.end(), [](const LitCar& a, const LitCar& b) { return a.d < b.d; });
    int nLit = 0;
    for (const LitCar& lc : lit) {
        if (nLit >= 14) break;
        nLit++;
        Vehicle& v = vehicles[lc.id];
        const Vehicles::VehicleModel& spec = vassets[v.model].spec;
        mat3 R = v.sim.body.rotMat();
        bool brake = v.seats[0] >= 0 && v.ctl.brake > 0.1f && v.sim.forwardSpeed() > 0.5f;
        for (const auto& L : spec.lights) {
            dvec3 lp = v.sim.body.pos + R * L.pos;
            if (L.type == Vehicles::LT_HEAD && v.lightsOn) {
                Render::DynamicLight dl;
                dl.pos = lp + R * vec3(0, 0.15f, 0);
                dl.dir = normalize(R * (length2(L.dir) > 0.1f ? L.dir : vec3(0, 1, -0.08f)));
                dl.color = vec3(1.f, 0.93f, 0.82f) * (night ? 9000.f : 3000.f);
                dl.radius = 45.f;
                dl.spotCos = cosf(32.f * kDegToRad);
                dl.spotInner = cosf(18.f * kDegToRad);
                dl.headlight = !isAircraft(lc.id) && !isBoat(lc.id);   // low-beam pattern with cut-off (renderer)
                renderer->addLight(dl);
            } else if ((L.type == Vehicles::LT_TAIL && v.lightsOn) || (L.type == Vehicles::LT_BRAKE && brake)) {
                Render::DynamicLight dl;
                dl.pos = lp - R * vec3(0, 0.2f, 0);
                dl.color = vec3(1.f, 0.05f, 0.02f) * (brake ? 220.f : 60.f);
                dl.radius = brake ? 5.f : 3.f;
                renderer->addLight(dl);
            } else if ((L.type == Vehicles::LT_SIREN_RED || L.type == Vehicles::LT_SIREN_BLUE) && v.sirenOn) {
                // same flash pattern as the lens shader (dynamic.hlsl): the left half of the bar is half a cycle ahead
                float ph = fmodf(env->gameSeconds * 2.2f + (L.pos.x < 0.f ? 0.5f : 0.f), 1.f);
                if (ph < 0.25f || (ph > 0.35f && ph < 0.55f)) {
                    Render::DynamicLight dl;
                    dl.pos = lp + vec3(0, 0, 0.2f);
                    dl.color = (L.type == Vehicles::LT_SIREN_RED ? vec3(1.f, 0.05f, 0.05f) : vec3(0.1f, 0.2f, 1.f)) * 1500.f;
                    dl.radius = 18.f;
                    renderer->addLight(dl);
                }
            }
        }
    }
    // ---- peds
    int copTorches = 0;
    for (int i = 0; i < (int)peds.size(); i++) {
        Ped& p = peds[i];
        if (!p.used || p.charIndex < 0) continue;
        if (p.visibleDist > 350.f) continue;
        if (hidePlayerModel && i == player) continue;
        if (i != player) {
            // a passer-by walking through the camera would fill the frame with the inside of their body: skip them
            vec3 rc = rel(rig.cam.pos, p.pos);
            float zc = Clamp(rc.z, 0.3f, 1.5f);
            if (length(vec3(rc.x, rc.y, rc.z - zc)) < 0.55f) continue;
        }
        vec3 toP = rel(p.pos, cam);
        if (dot(toP, camF) < -2.f && p.visibleDist > 3.f) continue;
        const CharEntry& ce = chars[p.charIndex];
        Render::DrawItem d;
        int clod = p.isPlayer ? 0 : (p.visibleDist < 15.f ? 0 : (p.visibleDist < 40.f ? 1 : 2));
        d.model = clod == 0 || !ce.lods[clod - 1] ? ce.model : ce.lods[clod - 1];
        d.pos = p.pos;
        d.rot = p.ragdoll ? mat3() : mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), p.yaw));
        d.bones = p.skin;
        d.boneCount = Anim::B_COUNT;
        for (int w = 0; w < 4; w++) d.wounds[w] = p.wounds[w];
        d.id = 0x500000000ull | p.uid;
        d.castShadow = p.visibleDist < 150.f;
        d.wetExposed = p.state == PS_SWIM ? 1.f : 0.6f;
        dyn->submit(d);
        if (p.moveMode == 4 && parachuteModel) {
            Render::DrawItem cd;
            cd.model = parachuteModel;
            cd.pos = p.pos + dvec3(0, 0, 0.3);
            cd.rot = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), p.yaw));
            float o = Saturate(p.chuteOpen);
            cd.scale = vec3(0.3f + 0.7f * o, 0.4f + 0.6f * o, 0.25f + 0.75f * o);
            cd.id = 0x700000000ull | p.uid;
            dyn->submit(cd);
        }
        // weapon in hand
        if (p.weapon != WPN_FISTS && weaponModels[p.weapon] && p.state != PS_INVEHICLE && p.state != PS_ENTERING && p.visibleDist < 120.f &&
            p.state != PS_SWIM) {
            mat3 pr = d.rot;
            vec3 hand = pr * p.bones[Anim::B_HAND_R].c[3].xyz();
            vec3 fore = pr * p.bones[Anim::B_FOREARM_R].c[3].xyz();
            if (p.ragdoll) {
                hand = p.bones[Anim::B_HAND_R].c[3].xyz() - p.pos.toVec3();
                fore = p.bones[Anim::B_FOREARM_R].c[3].xyz() - p.pos.toVec3();
            }
            vec3 dirF = normalize(hand - fore);
            vec3 fwd = p.aiming && !p.ragdoll ? p.aimDir : dirF;
            Render::DrawItem wd;
            int tint = p.isPlayer ? Clamp((int)pinfo.wpnTint[p.weapon], 0, kWeaponTints - 1) : 0;
            wd.model = weaponTintModels[p.weapon][tint] ? weaponTintModels[p.weapon][tint] : weaponModels[p.weapon];
            wd.pos = p.pos + (hand + dirF * 0.04f);
            wd.rot = frameFromForward(fwd, vec3(0, 0, 1));
            if (p.weapon == WPN_BAT || p.weapon == WPN_KNIFE) {
                // melee weapons follow the fist: grip point in the fist, handle axis towards the tip, roll from the palm
                vec3 gpos, gaxis, gpalm;
                Anim::handGrip(ce.skel, p.bones, true, gpos, gaxis, gpalm);
                vec3 wpos = p.ragdoll ? gpos - p.pos.toVec3() : pr * gpos;
                vec3 wax = normalize(p.ragdoll ? gaxis : pr * gaxis), wpalm = p.ragdoll ? gpalm : pr * gpalm;
                float gripOff = p.weapon == WPN_BAT ? 0.03f : 0.f;   // bat grip ~13 cm above the knob (model origin is 10 cm)
                wd.pos = p.pos + dvec3(wpos - wax * gripOff);
                wd.rot = frameFromForward(wax, wpalm);
            }
            bool hideGun = false;
            if (p.isPlayer && fpw.active) {   // first person: held in front of the eyes (fpweapon.cpp)
                wd.pos = fpw.pos;
                wd.rot = fpw.rot;
                hideGun = fpw.hideWeapon;     // looking through a scope
            }
            wd.id = 0x600000000ull | p.uid;
            wd.castShadow = p.visibleDist < 40.f;
            if (!hideGun) dyn->submit(wd);
            // fitted attachments share the weapon's transform
            u8 comps = weaponComps(p, p.weapon);
            for (int c = 0; c < kWeaponCompCount && comps && !hideGun; c++) {
                if (!(comps & (1 << c)) || !weaponCompModels[p.weapon][c]) continue;
                Render::DrawItem cd = wd;
                cd.model = weaponCompModels[p.weapon][c];
                cd.id = wd.id + ((u64)(c + 1) << 40);
                dyn->submit(cd);
            }
            // officers on foot sweep torches along their gun barrels at night while the player is wanted (the
            // first four within 70 m only: every beam is a shadowless spot light in the tiled pass)
            if (!p.isPlayer && isCop(p) && p.state == PS_ONFOOT && pinfo.wanted > 0 && p.visibleDist < 70.f &&
                nightTime(env->timeOfDay) && copTorches < 4) {
                copTorches++;
                Render::DynamicLight fl;
                vec3 bf = wd.rot * vec3(0.f, 1.f, 0.f);
                fl.pos = wd.pos + dvec3(bf * 0.2f);
                vec3 look = p.aiming ? p.aimDir : vec3(-sinf(p.yaw), cosf(p.yaw), -0.18f);
                float sweep = sinf((float)time * 0.9f + (float)(p.uid % 17u)) * (p.aiming ? 0.f : 0.35f);
                fl.dir = normalize(vec3(look.x * cosf(sweep) - look.y * sinf(sweep), look.x * sinf(sweep) + look.y * cosf(sweep), look.z));
                fl.color = vec3(1.f, 0.97f, 0.92f) * 4200.f;
                fl.radius = 28.f;
                fl.spotCos = cosf(15.f * kDegToRad);
                fl.spotInner = cosf(6.f * kDegToRad);
                renderer->addLight(fl);
            }
            // weapon flashlight: a narrow beam from the barrel while aiming in the dark (night or indoors)
            if ((comps & WC_FLASHLIGHT) && pinfo.flashlightOn && p.aiming && p.state == PS_ONFOOT &&
                (nightTime(env->timeOfDay) || Interiors::isInside())) {
                Render::DynamicLight fl;
                vec3 bf = wd.rot * vec3(0.f, 1.f, 0.f);
                fl.pos = wd.pos + dvec3(bf * 0.35f);
                fl.dir = normalize(p.aimDir);
                fl.color = vec3(1.f, 0.96f, 0.9f) * 5200.f;
                fl.radius = 32.f;
                fl.spotCos = cosf(17.f * kDegToRad);
                fl.spotInner = cosf(7.f * kDegToRad);
                renderer->addLight(fl);
            }
        }
        // smartphone in hand: at the ear during calls, browsing, the idle phone check (Anim::phoneFrame fits all)
        if (phoneModel && p.visibleDist < 35.f && !p.ragdoll && (p.state == PS_ONFOOT || p.state == PS_SWIM) &&
            (p.anim.phoneW > 0.5f || p.anim.browseW > 0.5f || (p.anim.idleVar == Anim::detail::IC_IDLE_PHONE && p.anim.idleVarW > 0.5f))) {
            mat3 pr = d.rot;
            Render::DrawItem pd;
            pd.model = phoneModel;
            vec3 fp, fl, fs;
            Anim::phoneFrame(ce.skel, p.bones, fp, fl, fs);
            pd.pos = p.pos + dvec3(pr * fp);
            pd.rot = frameFromForward(pr * fl, pr * fs);
            pd.id = 0x680000000ull | p.uid;
            pd.castShadow = p.visibleDist < 15.f;
            dyn->submit(pd);
        }
    }
    if (weaponShowcase) submitWeaponShowcase();
    Wildlife::submitRender(*this);   // birds, flocks, fish shoals, pets, herds, alligators (wildlife.cpp)
    Transit::submit(*this);          // train door leaves, far trains, head lamps (transit_game.cpp)
    // ---- pickups
    for (auto& pk : pickups) {
        if (!pk.used || pk.timer > 0.f) continue;
        float d = length(rel(pk.pos, cam));
        if (d > 150.f) continue;
        Render::Model* m = pk.type == PICK_WEAPON ? weaponModels[pk.weapon] : pickupModels[pk.type];
        if (!m) continue;
        Render::DrawItem di;
        di.model = m;
        float bob = sinf((float)time * 2.2f + (float)(size_t)(&pk - &pickups[0])) * 0.06f;
        di.pos = pk.pos + dvec3(0, 0, 0.25 + bob);
        di.rot = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), (float)time * 1.6f));
        di.castShadow = d < 40.f;
        di.emissiveScale = pk.type == PICK_COLLECTIBLE ? 1.f + 0.5f * sinf((float)time * 4.f) : 1.f;
        dyn->submit(di);
        if (d < 60.f) {
            vec3 col = pk.type == PICK_HEALTH ? vec3(1.f, 0.2f, 0.2f) : (pk.type == PICK_ARMOR ? vec3(0.3f, 0.5f, 1.f) : (pk.type == PICK_COLLECTIBLE ? vec3(1.f, 0.3f, 0.8f) : vec3(0.4f, 1.f, 0.5f)));
            spawnLight(pk.pos + dvec3(0, 0, 0.5), col * 90.f, 2.5f);
        }
    }
    // ---- projectiles
    for (auto& pr : projectiles) {
        if (!pr.used) continue;
        Render::DrawItem di;
        di.model = weaponModels[pr.type == PROJ_MOLOTOV ? WPN_MOLOTOV : WPN_GRENADE];
        if (!di.model) continue;
        di.pos = pr.pos;
        di.rot = frameFromForward(length2(pr.vel) > 0.01f ? pr.vel : vec3(0, 1, 0), vec3(0, 0, 1));
        dyn->submit(di);
        if (pr.type == PROJ_ROCKET) spawnLight(pr.pos, vec3(1.f, 0.6f, 0.25f) * 3000.f, 10.f);
        if (pr.type == PROJ_MOLOTOV) spawnLight(pr.pos, vec3(1.f, 0.5f, 0.15f) * 200.f, 3.f);
    }
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::updateAudioListener(float dt) {
#ifdef HAVE_AUDIO
    Audio::Listener L;
    L.pos = rig.cam.pos.toVec3();
    static vec3 prevPos = L.pos;
    L.vel = dt > 0.f ? (L.pos - prevPos) / dt : vec3(0);
    if (length(L.vel) > 120.f) L.vel = vec3(0);
    prevPos = L.pos;
    L.forward = rig.cam.forward();
    L.up = vec3(0, 0, 1);
    int pv = playerVehicle();
    if (pv >= 0 && !isBike(pv) && !isBoat(pv) && rig.vehicleView != 3) {
        L.inVehicle = rig.vehicleView == 2 ? 1.f : 0.35f;
        L.interior = rig.vehicleView == 2 ? 0.6f : 0.f;
    }
    L.interior = Max(L.interior, Interiors::insideAmount() * 0.8f);   // inside a building (interiors_game.cpp)
    Audio::update(L, dt);
    Audio::Ambience amb;
    vec2 p = L.pos.xy();
    World::Region reg = map->regionAt(p.x, p.y);
    const World::RegionInfo& ri = World::regionInfo(reg);
    (void)ri;
    float urban = 0.f, nature = 0.f, coast = 0.f, wet = 0.f;
    switch (reg) {
        case World::REG_DOWNTOWN: case World::REG_FINANCIAL: case World::REG_MIDTOWN: case World::REG_NORTH_CITY:
        case World::REG_CALLE_LUNA: case World::REG_PORT: case World::REG_AIRPORT: case World::REG_FLATS:
        case World::REG_FORT_CASTELL:
            urban = 1.f; break;
        case World::REG_BEACH: case World::REG_BAY_ISLAND: case World::REG_KEY_CORAL:
            urban = 0.6f; coast = 0.7f; break;
        case World::REG_GROVE: case World::REG_SUBURBS: case World::REG_REDLAND: case World::REG_LAKE_TOWN: case World::REG_HARLOW:
            urban = 0.5f; nature = 0.4f; break;
        case World::REG_SAWGRASS: case World::REG_GULF_TOWN:
            nature = 0.4f; wet = 1.f; break;
        case World::REG_OCEAN: case World::REG_KEYS: case World::REG_KEY_TOWN:
            coast = 1.f; nature = 0.2f; break;
        default:
            nature = 0.8f; urban = 0.15f; break;
    }
    // coast proximity via the coarse distance field
    float sd = map->coastDistance(p.x, p.y);
    if (sd < 250.f && sd > -400.f) coast = Max(coast, Saturate(1.f - fabsf(sd) / 250.f));
    amb.urban = urban;
    amb.nature = nature;
    amb.coast = coast;
    amb.wetland = wet;
    amb.rain = env->rain;
    amb.wind = Saturate(env->wind + Saturate((L.pos.z - 60.f) / 300.f));
    amb.timeOfDay = env->timeOfDay;
    float wz;
    amb.underwater = Phys::waterSurface(L.pos.x, L.pos.y, wz) && L.pos.z < wz - 0.1f ? 1.f : 0.f;
    Audio::setAmbience(amb);
    // crowd murmur bed: density from the people around the listener, flavour from the place
    {
        std::vector<int> near_;
        pedsNear(p, 25.f, near_);
        int n = 0, scared = 0;
        for (int id : near_) {
            const Ped& q = peds[id];
            if (q.isPlayer || q.health <= 0.f || q.state == PS_INVEHICLE) continue;
            n++;
            if (q.brain.type == BRAIN_FLEE || q.brain.type == BRAIN_COWER) scared++;
        }
        int ik = Interiors::currentKind();
        int place = ik == World::IK_CLUB || ik == World::IK_BAR ? Audio::CROWD_CLUB
                  : (reg == World::REG_BEACH || reg == World::REG_KEY_CORAL) && coast > 0.5f ? Audio::CROWD_BEACH
                  : Audio::CROWD_STREET;
        Audio::setCrowd(Saturate(n / 30.f), place, n > 0 ? Saturate((float)scared / (float)n) : 0.f);
    }
#else
    (void)dt;
#endif
}

// ------------------------------------------------------------------------------------------------------------------
// Gameplay FX -> renderer bridge
void spawnFx(FxType t, dvec3 pos, vec3 dir, int count, float scale, vec3 tint) {
    if (!gGame || !gGame->renderer) return;
    Render::ParticleType pt = Render::PT_SMOKE;
    switch (t) {
        case FX_SMOKE: pt = Render::PT_SMOKE; break;
        case FX_DARK_SMOKE: pt = Render::PT_DARK_SMOKE; break;
        case FX_DUST: pt = Render::PT_DUST; break;
        case FX_SPARKS: pt = Render::PT_SPARKS; break;
        case FX_FIRE: pt = Render::PT_FIRE; break;
        case FX_EXPLOSION: pt = Render::PT_EXPLOSION; break;
        case FX_BLOOD: pt = Render::PT_BLOOD; break;
        case FX_WATER_SPLASH: pt = Render::PT_WATER_SPLASH; break;
        case FX_WAKE_SPRAY: pt = Render::PT_WAKE_SPRAY; break;
        case FX_TIRE_SMOKE: pt = Render::PT_TIRE_SMOKE; break;
        case FX_EXHAUST: pt = Render::PT_EXHAUST; break;
        case FX_MUZZLE_FLASH: pt = Render::PT_MUZZLE_FLASH; break;
        case FX_GLASS: pt = Render::PT_GLASS; break;
        case FX_DEBRIS: pt = Render::PT_DEBRIS; break;
        case FX_LEAVES: pt = Render::PT_LEAVES; break;
        default: break;
    }
    gGame->renderer->spawnParticles(pt, pos, dir, count, scale, tint);
}

void spawnDecal(DecalKind k, dvec3 pos, vec3 normal, float size) {
    if (!gGame || !gGame->renderer) return;
    Render::DecalType dt = Render::DECAL_BULLET_CONCRETE;
    switch (k) {
        case DECAL_BULLET_CONCRETE: dt = Render::DECAL_BULLET_CONCRETE; break;
        case DECAL_BULLET_METAL: dt = Render::DECAL_BULLET_METAL; break;
        case DECAL_BULLET_GLASS: dt = Render::DECAL_BULLET_GLASS; break;
        case DECAL_BLOOD: dt = Render::DECAL_BLOOD; break;
        case DECAL_SCORCH: dt = Render::DECAL_SCORCH; break;
    }
    float ang = hashToFloat(hash32((u32)(pos.x * 97.0) ^ (u32)(pos.y * 57.0))) * kTwoPi;
    gGame->renderer->addDecal(dt, pos, normal, size, ang);
}

void spawnTracer(dvec3 from, dvec3 to) {
    if (!gGame || !gGame->renderer) return;
    gGame->renderer->addTracer(from, to);
}

void spawnSkid(int& track, dvec3 pos, vec3 normal, float width, float intensity) {
    if (!gGame || !gGame->renderer) return;
    if (track < 0) track = (int)(hash32(gGame->nextUid++) & 0x7fffffff);
    gGame->renderer->addSkidMark(track, pos, normal, width, intensity);
}

void spawnLight(dvec3 pos, vec3 color, float radius) {
    if (!gGame || !gGame->renderer) return;
    Render::DynamicLight dl;
    dl.pos = pos;
    dl.color = color;
    dl.radius = radius;
    gGame->renderer->addLight(dl);
}

}  // namespace Game
