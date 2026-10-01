// Tidegram hooks: turns what the player just did into social-feed events (UI::tidegramReport); NPC accounts post
// about them a little later. Observed every frame (wanted level changes, deaths and arrests, sustained speeding, low
// fly-bys) and reported directly at one-off events (explosions, stolen cars, crashes, gunfire, weather changes).
#include "gameworld.h"

namespace Game {

namespace social_detail {

struct SocialState {
    int lastWanted = 0;
    bool wasDead = false, wasBusted = false;
    float speedingT = 0.f, flybyT = 0.f;
    double lastSpeeding = -1e9, lastFlyby = -1e9, lastCrash = -1e9;
};
SocialState gSocial;

}  // namespace social_detail

using namespace social_detail;

void GameWorld::socialReport(int ev, dvec3 pos, const char* subject, float magnitude) {
#ifdef HAVE_GAME_UI
    UI::tidegramReport((UI::TideEvent)ev, pos.toVec3().xy(), subject, magnitude);
#else
    (void)ev;
    (void)pos;
    (void)subject;
    (void)magnitude;
#endif
}

void GameWorld::updateSocial(float dt) {
#ifdef HAVE_GAME_UI
    Ped* pl = playerPed();
    if (!pl) return;
    dvec3 pp = pl->pos;
    // wanted level: every new star, and a clean getaway
    if (pinfo.wanted > gSocial.lastWanted) socialReport(UI::TE_WANTED, pp, nullptr, (float)pinfo.wanted);
    else if (pinfo.wanted == 0 && gSocial.lastWanted > 0 && !pinfo.busted && pl->health > 0.f) socialReport(UI::TE_ESCAPED, pp);
    gSocial.lastWanted = pinfo.wanted;
    bool dead = pinfo.deathTimer > 0.f && !pinfo.busted;
    if (dead && !gSocial.wasDead) socialReport(UI::TE_WASTED, pp);
    if (pinfo.busted && !gSocial.wasBusted) socialReport(UI::TE_BUSTED, pp);
    gSocial.wasDead = dead;
    gSocial.wasBusted = pinfo.busted;
    int pv = playerVehicle();
    if (pv >= 0) {
        const Vehicle& v = vehicles[pv];
        float kmh = v.sim.speed() * 3.6f;
        if (isAircraft(pv)) {
            // buzzing the city: low and fast over built-up districts
            float agl = (float)v.sim.body.pos.z - Max(map->heightAt((float)pp.x, (float)pp.y), 0.f);
            bool town = buildings && map->regionAt((float)pp.x, (float)pp.y) != World::REG_SAWGRASS;
            gSocial.flybyT = agl < 45.f && kmh > 120.f && town ? gSocial.flybyT + dt : 0.f;
            if (gSocial.flybyT > 1.5f && time - gSocial.lastFlyby > 60.0) {
                gSocial.lastFlyby = time;
                socialReport(UI::TE_LOW_FLYBY, pp);
            }
        } else if (!isBoat(pv)) {
            gSocial.speedingT = kmh > 170.f ? gSocial.speedingT + dt : 0.f;
            if (gSocial.speedingT > 3.f && time - gSocial.lastSpeeding > 90.0) {
                gSocial.lastSpeeding = time;
                socialReport(UI::TE_SPEEDING, pp, nullptr, kmh);
            }
        }
    } else {
        gSocial.speedingT = gSocial.flybyT = 0.f;
    }
#else
    (void)dt;
#endif
}

// A hard hit on the player's car (called from the vehicle update with the impulse of the impact).
void GameWorld::socialCrash(int vi, float impulse) {
#ifdef HAVE_GAME_UI
    if (vi < 0 || vi != playerVehicle() || time - gSocial.lastCrash < 20.0) return;
    const Vehicle& v = vehicles[vi];
    float dv = impulse / Max(v.sim.body.mass, 200.f);   // velocity change of the impact
    float kmh = Max(v.sim.speed(), dv) * 3.6f;
    if (kmh < 60.f) return;
    gSocial.lastCrash = time;
    socialReport(UI::TE_CRASH, v.sim.body.pos, nullptr, kmh);
#else
    (void)vi;
    (void)impulse;
#endif
}

}  // namespace Game
