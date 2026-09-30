// Open world glue: per-frame update of everything around the missions (menus, scripted drivers, outfits, the new-game
// opening, story phone calls, shops, activities, economy, character switching, test automation) and the 2D overlay.
#include "missions.h"

namespace Game {

void updateMissionTest(GameWorld& g, float dt);

namespace ow_detail {

struct OpenWorldState {
    int lastOverlayFrames = -1;
    int overlayMissing = 100;       // frames without a drawMissionOverlay call
    float introTimer = 0.f;
    int lastDef = -1;
    u32 lastPlayerUid = 0;
};
OpenWorldState gOW;

bool overlayWired() { return gOW.overlayMissing < 4; }

}  // namespace ow_detail

using namespace ow_detail;

void updateOpenWorld(GameWorld& g, float dt) {
    using namespace mu;
    computePlaces(g);
    // overlay hook detection (the app calls drawMissionOverlay after the HUD)
    if (gMissions.overlayFrames != gOW.lastOverlayFrames) {
        gOW.lastOverlayFrames = gMissions.overlayFrames;
        gOW.overlayMissing = 0;
    } else {
        gOW.overlayMissing++;
    }
    gWorldMarkers.clear();
    // a new player ped that did not come from a character switch means a new game / loaded save: drop transient state
    if (Ped* p0 = g.playerPed()) {
        if (p0->uid != gOW.lastPlayerUid) {
            if (gOW.lastPlayerUid != 0 && !gSwitching) {
                gMissions.retry = RetryState();
                if (gMenu.open) menuClose(g);
                clearDrivers(g, 0);
                clearDrivers(g, 1);
                for (Safehouse& sh : gShops.safehouses) sh.garageVehicle = -1;
                gShops.activeShop = gShops.activeSafehouse = gShops.activeBusiness = -1;
                gShops.resprayStage = 0;
                phoneGameReset();
                g.phone.open = false;
                UI::Phone::reset();   // a loaded save / new game starts with the handset closed on its home screen
            }
            gOW.lastPlayerUid = p0->uid;
            gSwitching = false;
        }
    }
    menuUpdate(g, dt);
    updateDrivers(g, dt);
    enforceOutfit(g);
    Ped* pl = g.playerPed();
    // new game: the prologue starts with Tomas' phone call a few seconds in
    if (pl && !flag(g, EX_INTRO_DONE) && !gMissions.active && !Platform::argValue("mission") && !Platform::argValue("missiontest")) {
        gOW.introTimer += dt;
        if (gOW.introTimer > 4.f && g.playerControl && !g.mInCutscene()) {
            setFlag(g, EX_INTRO_DONE, 1);
            int di = gMissions.findDef("low_tide");
            if (di >= 0 && !storyDone(g, SF_LOW_TIDE)) {
                gMissions.startCheckpoint = 0;
                g.startMission(di);
            }
        }
    }
    if (pl) {
        shopsUpdate(g, dt);
        activitiesUpdate(g, dt);
        economyUpdate(g, dt);
    }
    drawMarkers(g, gWorldMarkers);
    updateMissionTest(g, dt);
    if (gMenu.open && !overlayWired()) menuFallback(g);
}

void openWorldOnMissionEnd(GameWorld& g, int def, bool passed) {
    using namespace mu;
    clearDrivers(g, 0);
    clearMissionPickups(g);
    if (menuIs(MO_CHOICE)) menuClose(g);
    showMeter(g, "DETECTION", 0.f);
    (void)def;
    (void)passed;
}

bool openWorldBusy() { return mu::gMenu.open; }

void drawMissionOverlay(GameWorld& g, float dt) {
    gMissions.overlayFrames++;
    float W = (float)UI::screenWidth(), H = (float)UI::screenHeight();
    mu::titleCardDraw(W, H, dt);
    mu::menuDraw(g, W, H);
}

}  // namespace Game
