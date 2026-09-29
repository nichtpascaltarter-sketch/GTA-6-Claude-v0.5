// Builds the HUD state (UI::HudState) from the game world every frame.
#include "gameworld.h"

namespace Game {

namespace hud_detail {

std::string gLastZone, gLastStreet;
float gLocationTimer = 0.f;
float gVehicleNameTimer = 0.f;
int gLastVehicle = -1;
long long gLastMoney = 0;
bool gMoneyInit = false;

}  // namespace hud_detail

using namespace hud_detail;

void GameWorld::fillHud(UI::HudState& h, float dt) {
    Ped* pl = playerPed();
    h = UI::HudState();
    if (!pl) return;
    vec3 pp = pl->pos.toVec3();
    h.playerPos = pp.xy();
    h.playerZ = pp.z;
    h.playerHeading = pl->yaw;
    int pv = playerVehicle();
    if (pv >= 0) {
        vec3 f = vehicles[pv].sim.forward();
        h.playerHeading = atan2f(-f.x, f.y);
    }
    h.cameraHeading = rig.cam.yaw;
    h.health = Saturate(pl->health / pl->maxHealth);
    h.armor = Saturate(pl->armor / 100.f);
    h.breath = pinfo.breath;
    h.stamina = pinfo.stamina;
    h.dead = pl->health <= 0.f;
    // money with animated deltas
    if (!gMoneyInit) {
        gLastMoney = pinfo.money;
        gMoneyInit = true;
    }
    h.money = pinfo.money;
    if (pinfo.money != gLastMoney) {
        h.moneyDelta = pinfo.money - gLastMoney;
        gLastMoney = pinfo.money;
    }
    // wanted
    h.wanted = pinfo.wanted;
    h.wantedSearching = pinfo.wanted > 0 && !pinfo.policeSeesPlayer;
    h.wantedCooldown = pinfo.wantedCooldown;
    if (pinfo.wanted > 0 && !pinfo.policeSeesPlayer) {
        h.searchAreaCenters.push_back(pinfo.lastSeenPos.toVec3().xy());
        h.searchAreaRadii.push_back(120.f + pinfo.wanted * 90.f);
    }
    // weapon
    const WeaponInfo& wi = weaponInfo(pl->weapon);
    h.showWeapon = pl->state == PS_ONFOOT || pl->aiming;
    h.weaponName = wi.name;
    h.weaponIcon = wi.hudIcon;
    h.ammoClip = pl->clip[pl->weapon];
    h.ammoTotal = pl->ammo[pl->weapon] - pl->clip[pl->weapon];
    h.reloading = pl->reloadTimer > 0.f;
    h.weaponWheelOpen = pinfo.weaponWheel;
    if (pinfo.weaponWheel) {
        h.wheelSlots.assign(8, "");
        h.wheelIcons.assign(8, -1);
        for (int w = 0; w < WPN_COUNT; w++) {
            if (!pl->hasWeapon[w]) continue;
            const WeaponInfo& x = weaponInfo((WeaponType)w);
            int slot = x.slot;
            // prefer the currently equipped weapon for its slot, else the strongest
            if (h.wheelSlots[slot].empty() || w == pl->weapon) {
                h.wheelSlots[slot] = x.clipSize > 0 ? StrFormat("%s  %d", x.name, pl->ammo[w]) : std::string(x.name);
                h.wheelIcons[slot] = x.hudIcon;
            }
        }
        h.wheelSelected = pinfo.wheelSel;
    }
    // aiming
    h.aiming = pl->aiming || (pl->state == PS_ONFOOT && wi.clipSize > 0 && ctl.attack.down);
    h.reticleSpread = (wi.spread * (pl->aiming ? 0.45f : 1.f) * (1.f + pl->spreadHeat)) * 900.f;
    if (h.aiming) {
        WorldHit hit;
        vec3 f = rig.cam.forward();
        if (raycast(rig.cam.pos + f * 1.2f, f, wi.range, hit, player, pv) && hit.ped >= 0) {
            const Ped& t = peds[hit.ped];
            bool friendly = t.faction == FAC_FRIEND;
            h.reticleOnFriendly = friendly;
            h.reticleOnEnemy = !friendly && t.health > 0.f;
        }
    }
    h.hitMarker = pinfo.hitMarker;
    h.killMarker = pinfo.killMarker;
    pinfo.killMarker = false;
    for (size_t i = 0; i < pinfo.damageDirs.size(); i++) h.damageDirections.push_back(pinfo.damageDirs[i]);
    // vehicle
    if (pv >= 0) {
        const Vehicle& v = vehicles[pv];
        h.inVehicle = true;
        if (gLastVehicle != pv) {
            gVehicleNameTimer = 4.f;
            gLastVehicle = pv;
        }
        h.vehicleName = vassets[v.model].spec.name;
        h.speedKmh = v.sim.speed() * 3.6f;
        h.showSpeedometer = true;
        h.aircraft = isAircraft(pv);
        if (h.aircraft) {
            float gz = groundHeight(pp.x, pp.y, pp.z);
            h.altitude = pp.z - Max(gz, 0.f);
        }
    } else {
        gLastVehicle = -1;
    }
    gVehicleNameTimer = Max(0.f, gVehicleNameTimer - dt);
    h.vehicleNameTimer = gVehicleNameTimer;
#ifdef HAVE_AUDIO
    int st = Audio::radioStation();
    if (st >= 0) {
        h.radioStation = Audio::radioStationName(st);
        h.radioTrack = Audio::radioNowPlaying(st);
    } else if (hudRadioTimer > 0.f) {
        h.radioStation = "Radio Off";
    }
#endif
    h.radioTimer = hudRadioTimer;
    // location
    World::Region reg = map->regionAt(pp.x, pp.y);
    std::string zone = World::regionInfo(reg).name;
    std::string street;
    float s = 0, dist = 0;
    int e = roads->nearestEdge(pp.xy(), 30.f, &s, &dist, nullptr);
    if (e >= 0) street = roads->edges[e].name;
    if (zone != gLastZone || (!street.empty() && street != gLastStreet)) {
        if (zone != gLastZone) gLocationTimer = 5.f;
        else if (pv >= 0) gLocationTimer = Max(gLocationTimer, 3.f);
        gLastZone = zone;
        if (!street.empty()) gLastStreet = street;
    }
    gLocationTimer = Max(0.f, gLocationTimer - dt);
    h.zoneName = zone;
    h.streetName = gLastStreet;
    h.locationTimer = gLocationTimer;
    h.timeOfDay = env->timeOfDay;
    h.day = gameDay;
    // messages
    hudHelpTimer = Max(0.f, hudHelpTimer - dt);
    if (hudHelpTimer > 0.f) h.helpText = hudHelp;
    h.objective = hudObjective;
    subTimer = Max(0.f, subTimer - dt);
    if (subTimer > 0.f && settingsSubtitles) {
        h.subtitle.speaker = subSpeaker;
        h.subtitle.text = subText;
        h.subtitle.speakerColor = subColor;
    }
    if (hudBigTime >= 0.f) {
        hudBigTime += dt;
        if (hudBigTime > 6.f) hudBigTime = -1.f;
        else {
            h.bigMessage = hudBig;
            h.bigMessageSub = hudBigSub;
            h.bigMessageColor = hudBigColor;
            h.bigMessageTime = hudBigTime;
        }
    }
    if (pinfo.deathTimer > 0.f) {
        h.bigMessage = "WASTED";
        h.bigMessageSub = "";
        h.bigMessageColor = 0xff2020ff;
        h.bigMessageTime = pinfo.deathTimer;
    }
    hudNoteTimer = Max(0.f, hudNoteTimer - dt);
    if (hudNoteTimer > 0.f) {
        h.notification = hudNote;
        h.notificationTitle = hudNoteTitle;
    }
    h.missionTimer = missionTimerHud;
    h.missionCounterLabel = missionCounterLabel;
    h.missionCounter = missionCounter;
    h.missionCounterMax = missionCounterMax;
    // blips: nearby police, enemies, pickups, vehicles of interest, plus mission/static blips
    for (int i = 0; i < (int)peds.size(); i++) {
        const Ped& p = peds[i];
        if (!p.used || i == player || p.health <= 0.f) continue;
        vec3 d = p.pos.toVec3() - pp;
        float dist2 = d.x * d.x + d.y * d.y;
        if (dist2 > 300.f * 300.f) continue;
        UI::Blip b;
        b.pos = p.pos.toVec3().xy();
        b.heightDiff = d.z;
        if (p.faction == FAC_POLICE && pinfo.wanted > 0) {
            b.icon = UI::BLIP_POLICE;
            b.flash = true;
            if (p.vehicle >= 0 && isAircraft(p.vehicle)) b.icon = UI::BLIP_POLICE_HELI;
            if (p.vehicle >= 0 && p.seat != 0) continue;
        } else if (p.faction == FAC_ENEMY || (p.brain.type == BRAIN_COMBAT && p.brain.target == player)) {
            b.icon = UI::BLIP_ENEMY;
            b.scale = 0.8f;
        } else if (p.faction == FAC_FRIEND) {
            b.icon = UI::BLIP_FRIEND;
        } else continue;
        b.edge = p.faction == FAC_ENEMY;
        h.blips.push_back(b);
    }
    for (const UI::Blip& b : staticBlips) h.blips.push_back(b);
    for (const UI::Blip& b : missionBlips) h.blips.push_back(b);
    h.hasWaypoint = hasWaypoint;
    h.waypoint = waypoint;
    if (hasWaypoint) {
        h.gpsRoute = gpsRoute;
        UI::Blip wb;
        wb.pos = waypoint;
        wb.icon = UI::BLIP_WAYPOINT;
        h.blips.push_back(wb);
    } else if (!missionRoute.empty()) {
        h.gpsRoute = missionRoute;
        h.gpsColor = 0xff33ddff;
    }
    h.radarVisible = hudVisible && settingsRadar;
    float spd = pv >= 0 ? vehicles[pv].sim.speed() : 0.f;
    h.radarZoom = 1.f + Saturate(spd / 45.f) * 0.9f + (pv >= 0 && isAircraft(pv) ? Saturate(h.altitude / 300.f) * 2.f : 0.f);
}

}  // namespace Game
