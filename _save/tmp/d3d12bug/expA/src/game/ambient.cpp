// Ambient sea and air traffic: boats and jetskis cruising the bay and the coast, tour/news helicopters circling
// downtown and the beach, planes crossing the sky at altitude. Far away they move kinematically along looping paths;
// boats near the player switch to full physics with a simple helm AI so they can be chased, rammed or hijacked.
#include "gameworld.h"

namespace Game {

namespace ambient_detail {

enum CraftKind : u8 { AK_BOAT = 0, AK_HELI, AK_PLANE };

struct AmbientPath {
    std::vector<vec3> pts;   // closed loop (boats/helis) or open line (planes)
    std::vector<float> cum;  // cumulative length
    float length = 0.f;
    bool loop = true;
    CraftKind kind = AK_BOAT;
    void finalize() {
        cum.assign(pts.size() + 1, 0.f);
        size_t n = loop ? pts.size() : pts.size() - 1;
        for (size_t i = 0; i < n; i++) cum[i + 1] = cum[i] + length_(pts[i], pts[(i + 1) % pts.size()]);
        length = cum[n];
    }
    static float length_(vec3 a, vec3 b) { return ::length(b - a); }
    // Catmull-Rom sample at arc length s
    vec3 sample(float s, vec3* tangent) const {
        size_t n = loop ? pts.size() : pts.size() - 1;
        if (loop) {
            s = fmodf(s, length);
            if (s < 0) s += length;
        } else
            s = Clamp(s, 0.f, length);
        size_t i = 0;
        while (i + 1 < n && cum[i + 1] < s) i++;
        float seg = Max(cum[i + 1] - cum[i], 1e-3f);
        float t = (s - cum[i]) / seg;
        auto P = [&](int k) {
            int m = (int)pts.size();
            if (loop) return pts[((k % m) + m) % m];
            return pts[Clamp(k, 0, m - 1)];
        };
        vec3 p0 = P((int)i - 1), p1 = P((int)i), p2 = P((int)i + 1), p3 = P((int)i + 2);
        float t2 = t * t, t3 = t2 * t;
        vec3 pos = (p1 * 2.f + (p2 - p0) * t + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * t2 + (p1 * 3.f - p0 - p2 * 3.f + p3) * t3) * 0.5f;
        if (tangent) {
            vec3 d = ((p2 - p0) + (p0 * 2.f - p1 * 5.f + p2 * 4.f - p3) * (2.f * t) + (p1 * 3.f - p0 - p2 * 3.f + p3) * (3.f * t2)) * 0.5f;
            *tangent = length2(d) > 1e-8f ? normalize(d) : vec3(0, 1, 0);
        }
        return pos;
    }
};

struct AmbientCraft {
    int vehicle = -1;
    int path = -1;
    float s = 0.f;
    float speed = 10.f;
    bool physics = false;
    float bank = 0.f;
    float prevYaw = 0.f;
};

std::vector<AmbientPath> gPaths;
std::vector<AmbientCraft> gCrafts;
bool gBuilt = false;
float gPlaneTimer = 0.f;

bool deepWater(const World::WorldMap& m, float x, float y, float minDepth) {
    float wl = m.waterAt(x, y);
    if (wl <= World::kNoWater + 1.f) return false;
    return wl - m.heightAt(x, y) > minDepth;
}

// Random closed loop inside a disc, every sample on water deep enough for boats.
bool makeWaterLoop(const World::WorldMap& m, vec2 center, float radius, u32 seed, AmbientPath& out) {
    for (int attempt = 0; attempt < 40; attempt++) {
        u32 h = hash32(seed + attempt * 7919u);
        int n = 6 + (int)(h % 5);
        std::vector<vec3> pts;
        float a0 = hashToFloat(h) * kTwoPi;
        bool ok = true;
        for (int i = 0; i < n && ok; i++) {
            float a = a0 + kTwoPi * i / n;
            float r = radius * (0.45f + 0.55f * hashToFloat(hash32(h + i * 131u)));
            vec2 p = center + vec2(cosf(a), sinf(a)) * r;
            if (!deepWater(m, p.x, p.y, 2.5f)) ok = false;
            pts.push_back(vec3(p, 0.f));
        }
        if (!ok) continue;
        // segments must stay on water
        for (int i = 0; i < n && ok; i++) {
            vec3 a = pts[i], b = pts[(i + 1) % n];
            for (int k = 1; k < 16; k++) {
                vec3 q = lerp(a, b, k / 16.f);
                if (!deepWater(m, q.x, q.y, 2.f)) {
                    ok = false;
                    break;
                }
            }
        }
        if (!ok) continue;
        out.pts = pts;
        out.loop = true;
        out.kind = AK_BOAT;
        out.finalize();
        return true;
    }
    return false;
}

}  // namespace ambient_detail

using namespace ambient_detail;

void GameWorld::updateAmbientTraffic(float dt) {
    Ped* pl = playerPed();
    if (!pl || populationOff) return;
    if (!gBuilt) {
        gBuilt = true;
        gPaths.clear();
        gCrafts.clear();
        // boat loops: the bay between downtown and Sol Beach, offshore east coast, the south bay, Lake Okahatchee
        const vec2 boatAreas[] = {vec2(4300, 400), vec2(4350, 2600), vec2(4250, -1600), vec2(6900, 1200), vec2(6600, -3200),
                                  vec2(2500, -6200), vec2(-5200, -6800), vec2(-3300, 6600)};
        const float boatRadius[] = {700, 800, 700, 1500, 1500, 1800, 1800, 1100};
        for (int i = 0; i < 8; i++) {
            AmbientPath p;
            if (makeWaterLoop(*map, boatAreas[i], boatRadius[i], 0xB0A7u + i * 97u, p)) gPaths.push_back(p);
        }
        // helicopter circuits: downtown towers, Sol Beach, the port
        const vec3 heliCenters[] = {vec3(2600, 1100, 190), vec3(5100, 1300, 150), vec3(4300, -800, 170)};
        for (int i = 0; i < 3; i++) {
            AmbientPath p;
            p.kind = AK_HELI;
            p.loop = true;
            for (int k = 0; k < 8; k++) {
                float a = kTwoPi * k / 8;
                float r = 450.f + 150.f * sinf(k * 1.7f + i);
                p.pts.push_back(heliCenters[i] + vec3(cosf(a) * r, sinf(a) * r, 25.f * sinf(k * 2.3f)));
            }
            p.finalize();
            gPaths.push_back(p);
        }
    }
    vec3 pp = pl->pos.toVec3();
    // ---- spawn crafts for paths that have none (and are near enough to matter)
    for (int pi = 0; pi < (int)gPaths.size(); pi++) {
        const AmbientPath& path = gPaths[pi];
        if (path.kind == AK_PLANE) continue;
        vec3 c = path.pts[0];
        float d = length(c.xy() - pp.xy());
        bool has = false;
        for (auto& cr : gCrafts)
            if (cr.path == pi && cr.vehicle >= 0 && vehicles[cr.vehicle].used) has = true;
        if (has || d > 3500.f) continue;
        Vehicles::VehicleClass cls = path.kind == AK_HELI ? Vehicles::VC_HELI : ((hash32(pi * 31u) % 4 == 0) ? Vehicles::VC_JETSKI : Vehicles::VC_BOAT);
        int model = findVehicleModel(cls, hash32(pi * 7u));
        if (model < 0 && cls == Vehicles::VC_JETSKI) model = findVehicleModel(Vehicles::VC_BOAT, pi);
        if (model < 0) continue;
        AmbientCraft cr;
        cr.path = pi;
        cr.s = hashToFloat(hash32(pi * 977u)) * path.length;
        cr.speed = path.kind == AK_HELI ? 32.f : (cls == Vehicles::VC_JETSKI ? 16.f : 9.f + (hash32(pi) % 6));
        vec3 tan;
        vec3 pos = path.sample(cr.s, &tan);
        float wz = 0.f;
        if (path.kind == AK_BOAT) Phys::waterSurface(pos.x, pos.y, wz);
        int vid = spawnVehicle(model, dvec3(pos.x, pos.y, path.kind == AK_BOAT ? wz : pos.z), atan2f(-tan.x, tan.y), true, FAC_CIVILIAN);
        if (vid < 0) continue;
        Vehicle& v = vehicles[vid];
        v.persistent = true;
        v.scripted = true;
        v.renderFar = true;
        v.sim.engineOn = true;
        if (path.kind == AK_HELI) v.sim.rotorSpeed = 1.f;
        int drv = v.seats[0];
        if (drv >= 0) peds[drv].brain.type = BRAIN_NONE;
        cr.vehicle = vid;
        gCrafts.push_back(cr);
    }
    // ---- planes crossing the sky
    gPlaneTimer -= dt;
    int planesAlive = 0;
    for (auto& cr : gCrafts)
        if (cr.vehicle >= 0 && vehicles[cr.vehicle].used && gPaths[cr.path].kind == AK_PLANE) planesAlive++;
    if (gPlaneTimer <= 0.f && planesAlive < 2) {
        gPlaneTimer = 45.f + hashToFloat(hash32((u32)(time * 10.0))) * 60.f;
        u32 h = hash32((u32)(time * 100.0) + 17u);
        float a = hashToFloat(h) * kTwoPi;
        vec2 dir(cosf(a), sinf(a));
        vec2 side(-dir.y, dir.x);
        float off = (hashToFloat(hash32(h)) - 0.5f) * 5000.f;
        float alt = 700.f + hashToFloat(hash32(h * 3u)) * 900.f;
        AmbientPath p;
        p.kind = AK_PLANE;
        p.loop = false;
        vec2 start = pp.xy() - dir * 9000.f + side * off, end = pp.xy() + dir * 9000.f + side * off;
        p.pts = {vec3(start, alt), vec3(lerp(start, end, 0.5f), alt + 40.f), vec3(end, alt)};
        p.finalize();
        int model = findVehicleModel(Vehicles::VC_PLANE, h);
        if (model >= 0) {
            gPaths.push_back(p);
            AmbientCraft cr;
            cr.path = (int)gPaths.size() - 1;
            cr.s = 0.f;
            cr.speed = 70.f;
            vec3 tan;
            vec3 pos = p.sample(0.f, &tan);
            int vid = spawnVehicle(model, dvec3(pos), atan2f(-tan.x, tan.y), false);
            if (vid >= 0) {
                vehicles[vid].persistent = true;
                vehicles[vid].scripted = true;
                vehicles[vid].renderFar = true;
                vehicles[vid].sim.engineOn = true;
                vehicles[vid].sim.rotorSpeed = 1.f;
                vehicles[vid].lightsOn = true;
                cr.vehicle = vid;
                gCrafts.push_back(cr);
            }
        }
    }
    // ---- move crafts
    for (size_t i = 0; i < gCrafts.size();) {
        AmbientCraft& cr = gCrafts[i];
        if (cr.vehicle < 0 || !vehicles[cr.vehicle].used) {
            gCrafts.erase(gCrafts.begin() + i);
            continue;
        }
        Vehicle& v = vehicles[cr.vehicle];
        const AmbientPath& path = gPaths[cr.path];
        vec3 vp = v.sim.body.pos.toVec3();
        float dist = length(vp - pp);
        bool playerAboard = false;
        for (int s = 0; s < 8; s++)
            if (v.seats[s] >= 0 && peds[v.seats[s]].isPlayer) playerAboard = true;
        // hijacked, shot or rammed: hand over to physics permanently
        if (playerAboard || v.sim.health < 900.f || v.seats[0] < 0) {
            if (v.scripted) {
                v.scripted = false;
                v.sim.sleeping = false;
                vec3 tan;
                path.sample(cr.s, &tan);
                v.sim.body.vel = tan * cr.speed;
            }
            v.persistent = playerAboard;
            gCrafts.erase(gCrafts.begin() + i);
            continue;
        }
        // planes leave after crossing
        if (path.kind == AK_PLANE && cr.s >= path.length) {
            despawnVehicle(cr.vehicle, true);
            gCrafts.erase(gCrafts.begin() + i);
            continue;
        }
        // despawn when very far (respawns when the player returns)
        if (path.kind != AK_PLANE && dist > 4200.f) {
            despawnVehicle(cr.vehicle, true);
            gCrafts.erase(gCrafts.begin() + i);
            continue;
        }
        cr.s += cr.speed * dt;
        vec3 tan;
        vec3 pos = path.sample(cr.s, &tan);
        float yaw = atan2f(-tan.x, tan.y);
        float yawRate = (yaw - cr.prevYaw);
        while (yawRate > kPi) yawRate -= kTwoPi;
        while (yawRate < -kPi) yawRate += kTwoPi;
        yawRate /= Max(dt, 1e-4f);
        cr.prevYaw = yaw;
        cr.bank = Lerp(cr.bank, Clamp(-yawRate * (path.kind == AK_PLANE ? 3.f : 0.5f), -0.6f, 0.6f), Saturate(dt * 2.f));
        quat q = quatAxisAngle(vec3(0, 0, 1), yaw);
        if (path.kind == AK_BOAT) {
            float wz = 0.f;
            vec3 nrm(0, 0, 1);
            Phys::waterSurface(pos.x, pos.y, wz, &nrm);
            pos.z = wz - 0.1f + Saturate(cr.speed / 18.f) * 0.15f;
            // pitch/roll with the waves, bow up a little when planing
            vec3 f = rotate(q, vec3(0, 1, 0)), r = rotate(q, vec3(1, 0, 0));
            float pitch = -dot(nrm, f) + 0.04f * Saturate(cr.speed / 12.f);
            float roll = dot(nrm, r) + cr.bank * 0.3f;
            q = q * quatAxisAngle(vec3(1, 0, 0), pitch) * quatAxisAngle(vec3(0, 1, 0), roll);
        } else {
            q = q * quatAxisAngle(vec3(0, 1, 0), cr.bank) * quatAxisAngle(vec3(1, 0, 0), path.kind == AK_HELI ? -0.08f : 0.03f);
        }
        v.sim.body.pos = dvec3(pos);
        v.sim.body.rot = q;
        v.sim.body.vel = tan * cr.speed;
        v.sim.body.angVel = vec3(0);
        v.sim.inWater = path.kind == AK_BOAT;
        v.sim.throttleOut = 0.6f;
        v.sim.engineRpm = 2600.f + cr.speed * 60.f;
        if (path.kind != AK_BOAT) {
            v.sim.rotorSpeed = 1.f;
            v.sim.rotorAngle += dt * (path.kind == AK_HELI ? 38.f : 60.f);
            v.sim.tailRotorAngle += dt * 90.f;
        }
        if (path.kind == AK_BOAT && dist < 300.f && hash32((u32)(time * 20.0) + cr.vehicle) % 3 == 0)
            spawnFx(FX_WAKE_SPRAY, dvec3(pos) + dvec3(-tan * 3.f), vec3(0, 0, 1.2f) - tan * 2.f, 1, Saturate(cr.speed / 20.f) + 0.3f);
        i++;
    }
}

// Public-address announcements in the airport terminal area (boarding calls, arrivals, security notices), spoken
// through the speech synthesizer's [pa] channel (band-limited hall sound with a long reverb tail).
void GameWorld::updatePublicAddress(float dt) {
#ifdef HAVE_AUDIO
    static float timer = 12.f;
    static u32 counter = 0;
    timer -= dt;
    if (timer > 0.f) return;
    timer = 38.f + hashToFloat(hash32(counter * 7919u + 3u)) * 40.f;
    const vec3 terminal(628.f, 1440.f, 9.f);
    Ped* pl = playerPed();
    if (!pl || length(rel(pl->pos, dvec3(terminal))) > 420.f) return;
    counter++;
    u32 h = hash32(counter * 2654435761u + 17u);
    static const char* const kAirlines[] = {"Palmera Air", "Coastline Airways", "Gulfwind", "Blue Heron Air", "Isla Pacifica"};
    static const char* const kPlaces[] = {"Isla Marena", "Kingsport", "Havenbrook", "Saint Corvin", "Vireo Bay", "Port Adelaide",
                                          "Coral Keys", "Mesa Verde", "Northgate", "San Lucero"};
    const char* airline = kAirlines[h % 5];
    const char* place = kPlaces[(h >> 4) % 10];
    int flight = 100 + (int)((h >> 8) % 880), gate = 1 + (int)((h >> 18) % 24);
    std::string num;
    for (char ch : std::to_string(flight)) {   // "2 1 7" is read digit by digit like a real announcer
        if (!num.empty()) num += ' ';
        num += ch;
    }
    std::string line;
    switch ((h >> 24) % 6) {
        case 0: line = StrFormat("%s flight %s to %s is now boarding at gate %d.", airline, num.c_str(), place, gate); break;
        case 1: line = StrFormat("This is the final boarding call for %s flight %s to %s. Please proceed to gate %d.", airline, num.c_str(), place, gate); break;
        case 2: line = StrFormat("%s flight %s from %s has arrived at gate %d.", airline, num.c_str(), place, gate); break;
        case 3: line = "Attention please. Unattended baggage will be removed by airport security."; break;
        case 4: line = StrFormat("%s flight %s to %s has been delayed. We apologize for the inconvenience.", airline, num.c_str(), place); break;
        default: line = "Welcome to Porto Sol International. Ground transportation is available on the lower level."; break;
    }
    Speech::Persona ann = Speech::persona((h >> 3) & 1 ? "announcer_female" : "announcer");
    Audio::speakAt(("[pa][calm]" + line).c_str(), ann.voice, terminal, 1.f);
#else
    (void)dt;
#endif
}

}  // namespace Game
