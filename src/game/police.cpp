// Crime reporting and the wanted level: crimes seen by police (or reported by civilian witnesses) raise the wanted
// level; breaking line of sight outside the search area lets it cool down.
#include "gameworld.h"

namespace Game {

namespace police_detail {

// crime types: 0 assault, 1 gunfire, 2 hit pedestrian with vehicle, 3 ram vehicle, 4 damage police vehicle,
// 5 assault officer, 6 kill officer, 7 murder, 8 explosion, 9 carjacking, 10 vehicle theft
const float kCrimeHeat[] = {0.35f, 0.5f, 0.6f, 0.15f, 0.9f, 1.2f, 2.2f, 1.2f, 1.4f, 0.45f, 0.25f};
const bool kNeedsWitness[] = {true, false, true, true, false, false, false, true, false, true, true};

}  // namespace police_detail

using namespace police_detail;

void GameWorld::reportCrime(int type, dvec3 pos, int victim) {
    if (player < 0 || type < 0 || type > 10) return;
    CrimeEvent e;
    e.type = type;
    e.pos = pos;
    e.victim = victim;
    crimes.push_back(e);
}

void GameWorld::updateWanted(float dt) {
    Ped* pl = playerPed();
    if (!pl) {
        crimes.clear();
        return;
    }
    vec3 ppos = pl->pos.toVec3();
    // police visibility of the player
    bool seen = false;
    for (int i = 0; i < (int)peds.size(); i++) {
        const Ped& p = peds[i];
        if (!p.used || p.faction != FAC_POLICE || p.health <= 0.f) continue;
        vec3 d = ppos - p.pos.toVec3();
        float dist = length(d);
        float range = p.state == PS_INVEHICLE ? 90.f : 70.f;
        if (p.vehicle >= 0 && isAircraft(p.vehicle)) range = 220.f;
        if (dist > range) continue;
        // vision cone for officers on foot (360 for helicopters/vehicles with sirens)
        if (p.state != PS_INVEHICLE && dist > 12.f) {
            vec3 f(-sinf(p.yaw), cosf(p.yaw), 0);
            if (dot(normalize(vec3(d.x, d.y, 0)), f) < 0.2f) continue;
        }
        if (lineOfSight(p.pos + dvec3(0, 0, 1.6), pl->pos + dvec3(0, 0, 1.2), i, p.vehicle)) {
            seen = true;
            break;
        }
    }
    pinfo.policeSeesPlayer = seen;
    if (seen) {
        pinfo.lastSeenPos = pl->pos;
        pinfo.lastSeenTime = (float)time;
    }
    // process crimes
    for (const CrimeEvent& e : crimes) {
        float heat = kCrimeHeat[e.type];
        bool witnessed = !kNeedsWitness[e.type] || seen;
        if (!witnessed) {
            // civilian witnesses nearby call it in (probabilistic, delayed via heat scaling)
            std::vector<int> w;
            pedsNear(vec2((float)e.pos.x, (float)e.pos.y), 35.f, w);
            int civ = 0;
            for (int i : w)
                if (!peds[i].isPlayer && peds[i].faction == FAC_CIVILIAN && peds[i].health > 0.f && i != e.victim) civ++;
            if (civ > 0) {
                witnessed = true;
                heat *= 0.6f;
            }
        }
        // gunfire only matters near people or police
        if (e.type == 1 && !seen) {
            std::vector<int> w;
            pedsNear(vec2((float)e.pos.x, (float)e.pos.y), 60.f, w);
            if (w.size() <= 1) witnessed = false;
            heat *= 0.5f;
        }
        if (!witnessed) continue;
        pinfo.wantedHeat += heat;
        pinfo.wantedCooldown = 0.f;
        if (seen || pinfo.wanted == 0) {
            pinfo.lastSeenPos = pl->pos;
            pinfo.lastSeenTime = (float)time;
        }
    }
    crimes.clear();
    // heat -> stars (thresholds), never decreases while being seen
    int stars = pinfo.wanted;
    const float thr[6] = {0.f, 0.3f, 2.0f, 5.0f, 9.0f, 15.0f};
    int target = 0;
    for (int s = 5; s >= 1; s--)
        if (pinfo.wantedHeat >= thr[s]) {
            target = s;
            break;
        }
    if (target > stars) {
        pinfo.wanted = target;
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_WANTED_UP, 0.7f);
#endif
    }
    pinfo.maxWanted = Max(pinfo.maxWanted, (float)pinfo.wanted);
    // Busted: low wanted level, an officer right next to a slow, non-shooting player on foot (or stopped in a car)
    static float bustTimer = 0.f;
    bool bustable = false;
    if (pinfo.wanted > 0 && pinfo.wanted <= 2 && pl->health > 0.f && !pinfo.busted) {
        int pv = pl->vehicle;
        float spd = pv >= 0 ? vehicles[pv].sim.speed() : length(vec2(pl->vel.x, pl->vel.y));
        bool calm = spd < 1.2f && !pl->firing && !(pv < 0 && pl->aiming);
        if (calm) {
            for (int i = 0; i < (int)peds.size(); i++) {
                const Ped& p = peds[i];
                if (!p.used || p.faction != FAC_POLICE || p.health <= 0.f || p.state != PS_ONFOOT) continue;
                if (length(p.pos.toVec3() - ppos) < (pv >= 0 ? 3.2f : 2.2f)) {
                    bustable = true;
                    break;
                }
            }
        }
    }
    bustTimer = bustable ? bustTimer + dt : 0.f;
    if (bustTimer > 2.2f) {
        bustTimer = 0.f;
        pinfo.busted = true;
        pinfo.arrests++;
        bigMessage("BUSTED", "", 0xffffcc33u);
#ifdef HAVE_AUDIO
        Audio::play2D(Audio::SFX_BUSTED, 0.9f);
#endif
        if (pl->vehicle >= 0) removePedFromVehicle(player, false);
        pl->pendingAction = Anim::CLIP_HANDS_UP;
        playerControl = false;
        pinfo.deathTimer = 0.001f;   // reuse the respawn flow (police station release)
    }
    if (pinfo.wanted > 0) {
        // evasion: out of sight and outside the search radius around the last seen position
        float searchR = 120.f + pinfo.wanted * 90.f;
        float distFromLast = length(rel(pl->pos, pinfo.lastSeenPos));
        if (!seen && (time - pinfo.lastSeenTime > 3.0 || distFromLast > searchR)) {
            float need = 8.f + pinfo.wanted * 6.f;
            pinfo.wantedCooldown += dt / need * (distFromLast > searchR ? 1.6f : 0.6f);
            if (pinfo.wantedCooldown >= 1.f) {
                pinfo.wanted = 0;
                pinfo.wantedHeat = 0.f;
                pinfo.wantedCooldown = 0.f;
#ifdef HAVE_AUDIO
                Audio::play2D(Audio::SFX_WANTED_LOST, 0.7f);
#endif
            }
        } else if (seen) {
            pinfo.wantedCooldown = 0.f;
        }
        // heat slowly relaxes toward the current star floor
        pinfo.wantedHeat = Max(thr[pinfo.wanted], pinfo.wantedHeat - dt * 0.02f);
    } else {
        pinfo.wantedHeat = Max(0.f, pinfo.wantedHeat - dt * 0.05f);
    }
}

}  // namespace Game
