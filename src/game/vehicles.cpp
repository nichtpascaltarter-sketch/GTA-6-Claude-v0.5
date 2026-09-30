// Vehicle entities: spawning, simulation stepping, collisions with peds/vehicles, audio emitters, lights, damage.
#include "gameworld.h"

namespace Game {

namespace veh_detail {

bool nightTime(float tod) { return tod < 6.8f || tod > 19.2f; }

Audio::EngineKind engineKindFor(const Vehicles::VehicleModel& m) { return (Audio::EngineKind)Clamp(m.engineSound, 0, (int)Audio::ENGINE_COUNT - 1); }

}  // namespace veh_detail

using namespace veh_detail;

bool GameWorld::isAircraft(int veh) const {
    if (veh < 0) return false;
    auto c = vassets[vehicles[veh].model].spec.cls;
    return c == Vehicles::VC_PLANE || c == Vehicles::VC_HELI;
}
bool GameWorld::isBoat(int veh) const {
    if (veh < 0) return false;
    auto c = vassets[vehicles[veh].model].spec.cls;
    return c == Vehicles::VC_BOAT || c == Vehicles::VC_JETSKI || c == Vehicles::VC_AIRBOAT;
}
bool GameWorld::isBike(int veh) const {
    if (veh < 0) return false;
    auto c = vassets[vehicles[veh].model].spec.cls;
    return c == Vehicles::VC_MOTORBIKE || c == Vehicles::VC_SCOOTER || c == Vehicles::VC_JETSKI;
}

void GameWorld::vehiclesNear(vec2 c, float r, std::vector<int>& out) const {
    out.clear();
    float r2 = r * r;
    for (int i = 0; i < (int)vehicles.size(); i++) {
        const Vehicle& v = vehicles[i];
        if (!v.used) continue;
        float dx = (float)v.sim.body.pos.x - c.x, dy = (float)v.sim.body.pos.y - c.y;
        if (dx * dx + dy * dy <= r2) out.push_back(i);
    }
}

int GameWorld::spawnVehicle(int model, dvec3 pos, float yaw, bool withDriver, Faction driverFaction) {
    if (model < 0 || model >= (int)vassets.size()) return -1;
    int id = -1;
    for (int i = 0; i < (int)vehicles.size(); i++)
        if (!vehicles[i].used) { id = i; break; }
    if (id < 0) {
        id = (int)vehicles.size();
        vehicles.emplace_back();
    }
    Vehicle& v = vehicles[id];
    v = Vehicle();
    v.used = true;
    v.uid = nextUid++;
    v.model = model;
    const Vehicles::VehicleModel& spec = vassets[model].spec;
    Vehicles::initVehicle(v.sim, spec, model, pos, yaw);
    u32 h = hash32(v.uid * 0x9E3779B9u);
    if (spec.fixedLivery) {
        v.color0 = spec.liveryPrimary;
        v.color1 = spec.liverySecondary;
    } else if (!spec.paletteColors.empty()) {
        v.color0 = spec.paletteColors[h % spec.paletteColors.size()];
        v.color1 = (h >> 8) % 3 == 0 ? spec.paletteColors[(h >> 12) % spec.paletteColors.size()] : vec3(0.05f, 0.05f, 0.055f);
    }
    v.dirt = ((h >> 16) & 255) / 255.f * 0.45f;
    v.faction = spec.cls == Vehicles::VC_POLICE ? FAC_POLICE : FAC_CIVILIAN;
    v.cruiseSpeed = 9.f + ((h >> 20) & 15) * 0.5f;
    v.radio = (int)((h >> 4) % 8);
    if (withDriver && !chars.empty()) {
        int role = driverFaction == FAC_POLICE ? 1 : (driverFaction == FAC_MEDIC ? 6 : (driverFaction == FAC_GANG_CUERVOS || driverFaction == FAC_GANG_SAINTS ? 2 : 0));
        int ci = randomCivilianChar(h >> 3, role);
        int pid = spawnPed(ci, pos, yaw, driverFaction);
        if (pid >= 0) warpPedIntoVehicle(pid, id, 0);
    }
    return id;
}

void GameWorld::despawnVehicle(int id, bool includeOccupants) {
    if (id < 0 || id >= (int)vehicles.size() || !vehicles[id].used) return;
    Vehicle& v = vehicles[id];
    for (int s = 0; s < 8; s++) {
        int pid = v.seats[s];
        if (pid < 0) continue;
        if (includeOccupants && !peds[pid].isPlayer) despawnPed(pid);
        else removePedFromVehicle(pid, false);
        v.seats[s] = -1;
    }
#ifdef HAVE_AUDIO
    Audio::EmitterHandle* hs[] = {&v.sndEngine, &v.sndSiren, &v.sndSkid, &v.sndHorn, &v.sndExtra, &v.sndAlarm};
    for (auto* hh : hs)
        if (*hh) {
            Audio::destroyEmitter(*hh);
            *hh = 0;
        }
#endif
    v.used = false;
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::updateVehicles(float dt) {
    dvec3 cam = rig.cam.pos;
    // Substep at <= 1/120 s
    int sub = Clamp((int)ceilf(dt / (1.f / 120.f)), 1, 4);
    float h = dt / sub;
    for (auto& v : vehicles)
        if (v.used) v.visibleDist = length(rel(v.sim.body.pos, cam));
    for (int s = 0; s < sub; s++) {
        for (int i = 0; i < (int)vehicles.size(); i++) {
            Vehicle& v = vehicles[i];
            if (!v.used) continue;
            if (v.scripted) continue;  // moved by gameplay (ambient.cpp)
            Vehicles::VehicleControls c = v.ctl;
            if (v.seats[0] < 0 || v.sim.wrecked) {
                // no driver: handbrake on, engine idles down
                c = Vehicles::VehicleControls();
                c.handbrake = true;
                c.brake = v.sim.speed() < 2.f ? 1.f : 0.3f;
                c.engineOff = v.parked || v.sim.wrecked;
                c.hasDriver = false;
            }
            Vehicles::stepVehicle(v.sim, c, h);
            if (v.sim.shifted) v.shiftLatch = true;
            handleVehicleEvents(i);
        }
        // vehicle-vehicle collisions (broad phase by distance)
        for (int i = 0; i < (int)vehicles.size(); i++) {
            Vehicle& a = vehicles[i];
            if (!a.used) continue;
            vec3 ha = vassets[a.model].spec.boxHalf;
            float ra = length(ha);
            for (int j = i + 1; j < (int)vehicles.size(); j++) {
                Vehicle& b = vehicles[j];
                if (!b.used) continue;
                if (a.sim.sleeping && b.sim.sleeping) continue;
                float rb = length(vassets[b.model].spec.boxHalf);
                vec3 d = rel(b.sim.body.pos, a.sim.body.pos);
                if (length2(d) > (ra + rb) * (ra + rb)) continue;
                if (Vehicles::collideVehicles(a.sim, b.sim)) {
                    a.sim.sleeping = b.sim.sleeping = false;
                    float imp = Max(a.sim.impactImpulse, b.sim.impactImpulse);
                    // crime: player rams vehicles
                    int pv = playerVehicle();
                    if ((i == pv || j == pv) && imp > 4000.f) reportCrime(3, a.sim.body.pos, i == pv ? j : i);
                }
            }
        }
    }
    // Ped vs vehicle interaction
    for (int vi = 0; vi < (int)vehicles.size(); vi++) {
        Vehicle& v = vehicles[vi];
        if (!v.used) continue;
        const Vehicles::VehicleModel& spec = vassets[v.model].spec;
        vec3 half = spec.boxHalf + vec3(0.3f, 0.3f, 0.f);
        mat3 R = v.sim.body.rotMat(), Rt = transpose(R);
        vec3 center = rel(v.sim.body.pos, dvec3(0, 0, 0)) + R * spec.boxCenter;
        float rad = length(half) + 0.5f;
        for (int pi = 0; pi < (int)peds.size(); pi++) {
            Ped& p = peds[pi];
            if (!p.used || p.vehicle == vi || p.state == PS_INVEHICLE) continue;
            if (p.state == PS_DEAD && !p.ragdoll) continue;
            vec3 pp = p.pos.toVec3() + vec3(0, 0, 0.9f);
            vec3 d = pp - center;
            if (length2(d) > rad * rad) continue;
            vec3 l = Rt * d;
            if (fabsf(l.x) > half.x || fabsf(l.y) > half.y || fabsf(l.z) > half.z + 0.9f) continue;
            // minimal push axis (horizontal)
            float px = half.x - fabsf(l.x), py = half.y - fabsf(l.y);
            vec3 nLocal = px < py ? vec3(l.x >= 0 ? 1.f : -1.f, 0, 0) : vec3(0, l.y >= 0 ? 1.f : -1.f, 0);
            float depth = Min(px, py);
            vec3 n = R * nLocal;
            n.z = 0;
            n = length2(n) > 1e-6f ? normalize(n) : vec3(1, 0, 0);
            vec3 pvRel = v.sim.body.pointVelocity(pp - rel(v.sim.body.pos, dvec3(0, 0, 0)));
            float closing = dot(pvRel - p.vel, n);
            if (p.state == PS_RAGDOLL || p.state == PS_DEAD) continue;  // ragdoll collides in ragdoll.cpp
            if (closing > 3.2f) {
                // hit by the vehicle: knock down with impulse, damage scales with speed^2
                float dmg = closing * closing * 1.1f;
                int driver = v.seats[0];
                knockDown(pi, (n * closing * 1.1f + vec3(0, 0, closing * 0.35f + 1.5f)) * 70.f);
                damagePed(pi, dmg, DMG_VEHICLE, driver, n);
                v.sim.body.vel -= n * (closing * 70.f / v.sim.body.mass);
#ifdef HAVE_AUDIO
                Audio::play(Audio::SFX_BODY_FALL, pp, Saturate(closing / 10.f));
                Audio::play(Audio::SFX_CAR_CRASH_LIGHT, pp, 0.5f);
#endif
                if (driver >= 0 && peds[driver].isPlayer) reportCrime(2, p.pos, pi);
            } else {
                p.pos = p.pos + n * depth;
                float vn = dot(p.vel, n);
                if (vn < dot(pvRel, n)) p.vel += n * (dot(pvRel, n) - vn);
            }
        }
    }
    for (auto& v : vehicles)
        if (v.used) updateVehicleFx(v, dt);
}

// Simulation events: rider ejection, broken street furniture, scraping, water entry.
void GameWorld::handleVehicleEvents(int vi) {
    Vehicle& v = vehicles[vi];
    Vehicles::VehicleState& s = v.sim;
    // Severe crashes injure the occupants (delta-v from the largest impulse this step)
    if (s.impactImpulse > 0.f) {
        float dv = s.impactImpulse / Max(s.body.mass, 100.f);
        if (dv > 9.f) {
            float dmg = (dv - 9.f) * (isBike(vi) ? 9.f : 5.5f);
            for (int seat = 0; seat < 8; seat++) {
                int o = v.seats[seat];
                if (o < 0) continue;
                damagePed(o, dmg, DMG_VEHICLE, -1, s.impactNormal);
            }
            if (v.seats[0] >= 0 && peds[v.seats[0]].isPlayer) rig.shake = Max(rig.shake, Saturate(dv / 20.f));
        }
    }
    if (s.ejectRider) {
        for (int seat = 0; seat < 2; seat++) {
            int rider = v.seats[seat];
            if (rider < 0) continue;
            vec3 vel = s.body.vel;
            removePedFromVehicle(rider, false);
            knockDown(rider, vel * 70.f + vec3(0, 0, 180.f));
            damagePed(rider, Min(80.f, length(vel) * 3.f), DMG_VEHICLE, -1, normalize(vel + vec3(0, 0, 0.01f)));
        }
    }
    for (int k = 0; k < s.brokenCount && k < 4; k++) {
        dvec3 bp(s.brokenPos[k]);
        spawnFx(FX_DEBRIS, bp + dvec3(0, 0, 0.8), s.brokenVel[k] * 0.4f + vec3(0, 0, 2.f), 6, 0.8f);
        spawnFx(FX_SPARKS, bp + dvec3(0, 0, 0.6), vec3(0, 0, 2.f), 6, 0.6f);
#ifdef HAVE_AUDIO
        Audio::play(Audio::SFX_METAL_SCRAPE, s.brokenPos[k], 0.9f);
        Audio::play(Audio::SFX_CAR_CRASH_LIGHT, s.brokenPos[k], 0.7f);
#endif
        brokenProps.push_back({s.brokenIds[k], (float)time});
    }
    if (s.scrape > 0.3f && v.visibleDist < 120.f && hash32(v.uid + (u32)(time * 60.0)) % 3 == 0)
        spawnFx(FX_SPARKS, s.body.pos + s.scrapePoint, s.body.vel * 0.3f + vec3(0, 0, 1.f), 2, 0.5f);
    if (s.splash > 2.f) {
        spawnFx(FX_WATER_SPLASH, s.body.pos, vec3(0, 0, s.splash * 0.4f), 10, Saturate(s.splash / 8.f) + 0.4f);
#ifdef HAVE_AUDIO
        Audio::play(s.splash > 6.f ? Audio::SFX_SPLASH_BIG : Audio::SFX_SPLASH_SMALL, s.body.pos.toVec3(), 1.f);
#endif
    }
}

// Lights, audio emitters, particles, fire/explosion of wrecked vehicles.
void GameWorld::updateVehicleFx(Vehicle& v, float dt) {
    const VehicleAsset& a = vassets[v.model];
    const Vehicles::VehicleModel& spec = a.spec;
    Vehicles::VehicleState& s = v.sim;
    bool driven = v.seats[0] >= 0;
    bool isPlayerCar = driven && peds[v.seats[0]].isPlayer;
    vec3 pos = s.body.pos.toVec3();
    // Headlights: AI at night/rain; player toggles
    if (driven && !isPlayerCar) v.lightsOn = nightTime(env->timeOfDay) || env->rain > 0.3f;
    // Sirens for police vehicles when their driver is in pursuit (set by police module)
    // Audio
#ifdef HAVE_AUDIO
    bool audible = v.visibleDist < 260.f && !v.exploded;
    if (audible && (s.engineOn || s.rotorSpeed > 0.02f)) {
        if (!v.sndEngine) {
            Audio::EmitterType et = Audio::EMIT_ENGINE;
            if (spec.cls == Vehicles::VC_HELI) et = Audio::EMIT_ROTOR;
            else if (spec.cls == Vehicles::VC_PLANE) et = Audio::EMIT_PROP;
            else if (spec.cls == Vehicles::VC_BOAT || spec.cls == Vehicles::VC_JETSKI) et = Audio::EMIT_BOAT;
            v.sndEngine = Audio::createEmitter(et);
        }
        float rpm01 = Saturate((s.engineRpm - 700.f) / Max(spec.maxRpm - 700.f, 1000.f));
        float thr = s.throttleOut;
        if (spec.cls == Vehicles::VC_HELI || spec.cls == Vehicles::VC_PLANE)
            Audio::setEmitter(v.sndEngine, pos, s.body.vel, s.rotorSpeed, thr, 0, 0, isPlayerCar ? 0.9f : 0.75f);
        else if (spec.cls == Vehicles::VC_BOAT || spec.cls == Vehicles::VC_JETSKI)
            Audio::setEmitter(v.sndEngine, pos, s.body.vel, rpm01, thr, s.inWater ? 1.f : 0.f, 0, isPlayerCar ? 0.9f : 0.7f);
        else
            Audio::setEmitter(v.sndEngine, pos, s.body.vel, rpm01, thr, s.engineLoad, (float)engineKindFor(spec), isPlayerCar ? 0.85f : 0.6f);
        if (spec.cls != Vehicles::VC_HELI && spec.cls != Vehicles::VC_PLANE && spec.cls != Vehicles::VC_BOAT &&
            spec.cls != Vehicles::VC_JETSKI) {
            // Tide Customs upgrades: sim-driven turbo whistle/blow-off and exhaust pops on hard shifts
            float tune = Saturate(Max(v.mods.turbo ? 0.6f : 0.f, (v.mods.engine + v.mods.transmission) / 6.f));
            if (tune > 0.f) Audio::setEngineTune(v.sndEngine, v.mods.turbo ? s.turboBoost : 0.f, tune, v.shiftLatch);
        }
    } else if (v.sndEngine) {
        Audio::destroyEmitter(v.sndEngine);
        v.sndEngine = 0;
    }
    // tire skid
    float slip = 0.f;
    int surf = 0;
    for (int w = 0; w < s.wheelCount; w++)
        if (s.wheels[w].contact) {
            slip = Max(slip, s.wheels[w].slip);
            if (s.wheels[w].surface != Phys::SURF_ASPHALT && s.wheels[w].surface != Phys::SURF_CONCRETE) surf = 1;
        }
    if (audible && slip > 0.25f && s.speed() > 2.f && !isBoat((int)(&v - &vehicles[0]))) {
        if (!v.sndSkid) v.sndSkid = Audio::createEmitter(Audio::EMIT_TIRE_SKID);
        Audio::setEmitter(v.sndSkid, pos, s.body.vel, Saturate((slip - 0.25f) * 1.6f), (float)surf, 0, 0, 0.8f);
    } else if (v.sndSkid) {
        Audio::destroyEmitter(v.sndSkid);
        v.sndSkid = 0;
    }
    if (audible && v.sirenOn && spec.sirenMode >= 0) {
        if (!v.sndSiren) v.sndSiren = Audio::createEmitter(Audio::EMIT_SIREN);
        Audio::setEmitter(v.sndSiren, pos, s.body.vel, (float)spec.sirenMode, 0, 0, 0, 1.f);
    } else if (v.sndSiren) {
        Audio::destroyEmitter(v.sndSiren);
        v.sndSiren = 0;
    }
    if (audible && v.hornOn) {
        if (!v.sndHorn) v.sndHorn = Audio::createEmitter(Audio::EMIT_HORN);
        Audio::setEmitter(v.sndHorn, pos, s.body.vel, (float)(v.uid % 7) / 7.f, 0, 0, 0, 1.f);
    } else if (v.sndHorn) {
        Audio::destroyEmitter(v.sndHorn);
        v.sndHorn = 0;
    }
    if (audible && v.alarm) {
        if (!v.sndAlarm) v.sndAlarm = Audio::createEmitter(Audio::EMIT_ALARM);
        Audio::setEmitter(v.sndAlarm, pos, s.body.vel, 0, 0, 0, 0, 0.9f);
    } else if (v.sndAlarm) {
        Audio::destroyEmitter(v.sndAlarm);
        v.sndAlarm = 0;
    }
    // impacts
    if (isPlayerCar && s.impactImpulse > 1500.f) rumble(Saturate(s.impactImpulse / 20000.f), Saturate(s.impactImpulse / 12000.f));
    v.lastImpactSfx -= dt;
    if (s.impactImpulse > 2500.f && v.lastImpactSfx <= 0.f && v.visibleDist < 200.f) {
        vec3 ip = pos + s.impactPoint;
        Audio::play(s.impactImpulse > 16000.f ? Audio::SFX_CAR_CRASH_HEAVY : Audio::SFX_CAR_CRASH_LIGHT, ip, Saturate(s.impactImpulse / 20000.f + 0.3f));
        if (s.impactImpulse > 22000.f && v.windowsBroken) Audio::play(Audio::SFX_GLASS_BREAK, ip, 0.7f);
        v.lastImpactSfx = 0.25f;
    }
#endif
    if (s.impactImpulse > 16000.f) socialCrash((int)(&v - &vehicles[0]), s.impactImpulse);
    // a violent crash shatters the windows
    if (s.impactImpulse > 30000.f && !v.windowsBroken) breakVehicleWindows((int)(&v - &vehicles[0]), normalize(s.impactPoint + vec3(0.f, 0.f, 0.01f)));
    // Skid marks: continuous strips per sliding wheel (a new strip starts when the wheel grips again)
    if (v.visibleDist < 120.f && !isBoat((int)(&v - &vehicles[0])) && !isAircraft((int)(&v - &vehicles[0]))) {
        for (int w = 0; w < s.wheelCount && w < 10; w++) {
            const Vehicles::WheelState& ws = s.wheels[w];
            bool marking = ws.contact && ws.slip > 0.42f && s.speed() > 2.5f && (ws.surface == Phys::SURF_ASPHALT || ws.surface == Phys::SURF_CONCRETE);
            if (marking) {
                float width = spec.wheels.size() > (size_t)w ? spec.wheels[w].width : 0.22f;
                spawnSkid(v.skidTrack[w], s.body.pos + ws.contactPos, ws.contactNormal, width, Saturate((ws.slip - 0.42f) * 2.f + 0.3f));
            } else {
                v.skidTrack[w] = -1;
            }
        }
    }
    // Exhaust puffs at idle / hard acceleration (cold air look), gear shift clunks
    if (v.visibleDist < 40.f && s.engineOn && !isBoat((int)(&v - &vehicles[0])) && !isAircraft((int)(&v - &vehicles[0]))) {
        v.exhaustTimer -= dt;
        if (v.exhaustTimer <= 0.f) {
            v.exhaustTimer = s.throttleOut > 0.6f ? 0.08f : 0.25f;
            vec3 back = rotate(s.body.rot, vec3(spec.boxHalf.x * 0.45f, spec.boxCenter.y - spec.boxHalf.y - 0.05f, 0.35f));
            spawnFx(FX_EXHAUST, s.body.pos + back, rotate(s.body.rot, vec3(0, -1.2f, 0.2f)), 1, 0.4f + s.throttleOut * 0.6f);
        }
    }
#ifdef HAVE_AUDIO
    if (v.shiftLatch && v.visibleDist < 60.f) Audio::play(Audio::SFX_GEAR_SHIFT, pos, isPlayerCar ? 0.5f : 0.25f);
#endif
    v.shiftLatch = false;
    // Particles: tire smoke, damaged engine smoke, fire, boat spray
    if (v.visibleDist < 150.f) {
        v.smokeTimer -= dt;
        if (v.smokeTimer <= 0.f) {
            v.smokeTimer = 0.05f;
            for (int w = 0; w < s.wheelCount; w++) {
                const Vehicles::WheelState& ws = s.wheels[w];
                // wet roads: tires throw a fine spray behind the car (more with speed and standing water)
                if (ws.contact && env->wetness > 0.25f && s.speed() > 7.f && v.visibleDist < 60.f && !isBoat((int)(&v - &vehicles[0])) &&
                    (ws.surface == Phys::SURF_ASPHALT || ws.surface == Phys::SURF_CONCRETE)) {
                    float wet = Saturate((env->wetness - 0.25f) / 0.5f) * Saturate((s.speed() - 7.f) / 20.f);
                    vec3 back = -v.sim.forward() * 0.6f + vec3(0.f, 0.f, 0.35f);
                    spawnFx(FX_WAKE_SPRAY, s.body.pos + ws.contactPos + dvec3(back * 0.3f), back * (0.5f + s.speed() * 0.05f), 1, 0.25f + 0.35f * wet,
                            vec3(0.82f, 0.84f, 0.86f));
                }
                if (ws.contact && ws.slip > 0.45f && s.speed() > 3.f && !isBoat((int)(&v - &vehicles[0]))) {
                    bool dusty = ws.surface == Phys::SURF_DIRT || ws.surface == Phys::SURF_SAND || ws.surface == Phys::SURF_GRASS || ws.surface == Phys::SURF_MUD;
                    spawnFx(dusty ? FX_DUST : FX_TIRE_SMOKE, s.body.pos + ws.contactPos, vec3(0, 0, 0.5f), 1,
                                             Saturate(ws.slip), dusty ? vec3(1) : v.mods.smoke);
                }
            }
            vec3 enginePos = rotate(s.body.rot, vec3(0, spec.boxCenter.y + spec.boxHalf.y * 0.7f, spec.boxCenter.z + spec.boxHalf.z * 0.6f));
            if (s.engineHealth < 350.f && !isBoat((int)(&v - &vehicles[0])))
                spawnFx(s.engineHealth < 120.f ? FX_DARK_SMOKE : FX_SMOKE, s.body.pos + enginePos, vec3(0, 0, 1.2f), 1,
                                         1.f, vec3(1));
            if (v.fireTimer > 0.f) spawnFx(FX_FIRE, s.body.pos + enginePos, vec3(0, 0, 1.5f), 2, 1.2f, vec3(1));
            if (isBoat((int)(&v - &vehicles[0])) && s.inWater && s.speed() > 4.f)
                spawnFx(FX_WAKE_SPRAY, s.body.pos + rotate(s.body.rot, vec3(0, spec.boxCenter.y - spec.boxHalf.y, 0.1f)),
                                         -s.body.vel * 0.2f + vec3(0, 0, 1.5f), 2, Saturate(s.speed() / 20.f), vec3(1));
        }
    }
    // Wreck / fire / explosion
    if ((s.health <= 0.f || s.engineHealth <= 0.f) && !v.exploded) {
        if (v.fireTimer <= 0.f && s.engineHealth <= 0.f) v.fireTimer = 0.001f;
    }
    if (v.fireTimer > 0.f && !v.exploded) {
        v.fireTimer += dt;
        if (v.fireTimer > 7.f) {
            v.exploded = true;
            explode(s.body.pos + vec3(0, 0, 0.8f), 9.f, 400.f, -1);
            s.wrecked = true;
            s.health = 0.f;
            v.color0 = v.color0 * 0.12f;
            v.color1 = v.color1 * 0.12f;
            v.dirt = 1.f;
            v.lightsOn = false;
            v.sirenOn = false;
            v.alarm = false;
        }
    }
    if (v.exploded) v.wreckTime += dt;
}

// ------------------------------------------------------------------------------------------------------------------
void GameWorld::damageVehicle(int veh, float amount, int attacker, vec3 pointRel, vec3 impulse) {
    if (veh < 0 || !vehicles[veh].used) return;
    Vehicle& v = vehicles[veh];
    amount *= 1.f - 0.15f * Min((int)v.mods.armor, 5);   // armor plating
    Vehicles::applyDamage(v.sim, amount, pointRel, impulse);
    v.sim.sleeping = false;
    if (attacker >= 0 && attacker < (int)peds.size() && peds[attacker].isPlayer && v.faction == FAC_POLICE) reportCrime(4, v.sim.body.pos, v.seats[0]);
    // occupants react
    int driver = v.seats[0];
    if (driver >= 0 && !peds[driver].isPlayer && attacker >= 0 && peds[driver].brain.type == BRAIN_DRIVER) {
        peds[driver].brain.alerted = true;
        peds[driver].brain.target = attacker;
    }
    if (!v.alarm && v.seats[0] < 0 && v.parked && amount > 5.f) v.alarm = true;
}

}  // namespace Game
