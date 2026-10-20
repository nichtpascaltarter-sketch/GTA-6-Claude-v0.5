// Mission framework: scripted story missions and side activities as small state machines driven by helper calls on
// GameWorld (objectives, targets with GPS, markers, dialogue with TTS + subtitles, cutscene camera shots, mission
// entities that are cleaned up automatically, pass/fail with rewards, checkpoints with a retry prompt).
// The open world around the missions (shops, safehouses, businesses, side activities, phone, character switching)
// lives in the story/economy modules and is updated from GameWorld::updateMissions via updateOpenWorld().
#pragma once
#include "gameworld.h"
#include "../audio/speech_ext.h"   // speech personas, inline style tags ([angry], [whisper]...) and displayText()

namespace Game {

enum MissionStatus : u8 { MS_RUNNING = 0, MS_PASSED, MS_FAILED };

struct DialogueLine {
    std::string speaker;     // subtitle name
    std::string text;        // spoken + shown (may carry speech tags like [angry] or [pause:0.5]; subtitles strip them)
    int ped = -1;            // speaking ped (positional voice); -1 = use voice preset below (radio/phone)
    bool female = false;     // preset voice gender when ped < 0
    u32 voiceSeed = 1;
    float pause = 0.25f;     // silence after the line
    u32 color = 0xff66ccff;  // speaker name color
    // Additions (missions module):
    std::string spoken;      // optional TTS text when it differs from the subtitle (phonetic spellings); empty = text
    bool phone = false;      // phone call: band-limited preset voice (ped < 0) and a phone icon in the subtitle
    bool hasVoice = false;   // use `voice` below instead of the ped voice / preset (story cast voices)
    Audio::VoiceParams voice;
};

struct CutsceneShot {
    dvec3 pos, target;
    dvec3 pos2, target2;     // camera moves linearly from pos/target to pos2/target2 over the shot
    float fov = 50.f;
    float duration = 3.f;
    int speaker = -2;        // shot added by the runtime to frame this speaking ped (-2: a scripted shot)
};

struct Marker {
    dvec3 pos;
    float radius = 2.f;
    vec3 color = vec3(1.f, 0.85f, 0.2f);
    bool vehicleSized = false;
};

struct MissionTest;

class Mission {
public:
    virtual ~Mission() {}
    virtual const char* title() const = 0;
    virtual const char* brief() const = 0;   // pause-menu mission log text
    virtual void start(GameWorld& g) = 0;    // sets up the mission; honors `checkpoint` (> 0 when retrying)
    virtual MissionStatus update(GameWorld& g, float dt) = 0;
    // Runs every frame before update() (shared per-frame bookkeeping of a mission family).
    virtual void preUpdate(GameWorld& g, float dt) {
        (void)g;
        (void)dt;
    }
    virtual long long reward() const { return 0; }
    // Automated test driver (--missiontest): performs the player's part of the current stage with scripted
    // teleports/controls. The default implementation handles generic targets, enemies and markers.
    virtual void autotest(GameWorld& g, MissionTest& t);
    // End-of-mission banners and retry policy (side activities override these).
    virtual const char* passBanner() const { return "MISSION PASSED"; }
    virtual const char* failBanner() const { return "MISSION FAILED"; }
    virtual bool allowRetry() const { return true; }
    // Called once when the mission ends (pass or fail) before the framework cleans up entities.
    virtual void finish(GameWorld& g, bool passed) {
        (void)g;
        (void)passed;
    }
    std::string failReason;
    int stage = 0;
    float stageTime = 0.f;
    int checkpoint = 0;       // checkpoint the mission was (re)started from
    void next() {
        stage++;
        stageTime = 0.f;
    }
    void setStage(int s) {
        stage = s;
        stageTime = 0.f;
    }
};

// Registered mission (story or side activity) with its start trigger.
struct MissionDef {
    const char* id;
    const char* contact;        // shown on the map legend
    char letter;                // blip letter
    vec2 startPos;              // trigger location
    int storyIndex;             // order in the story (-1 = side activity)
    int requiresFlag;           // storyFlags index that must be set (-1 none)
    int setsFlag;               // storyFlags index set when passed
    bool repeatable;
    float timeFrom, timeTo;     // availability window (hours; from == to -> always)
    UI::BlipIcon icon;
    std::function<Mission*()> create;
    // Additions:
    int protagonist = -1;       // -1 either, 0 Mari only, 1 Dex only
    int act = 0;                // story act (0 prologue, 1..3)
    const char* title = "";     // mission title (blip legend, replay list)
    int requiresFlag2 = -1;     // optional second prerequisite
    u32 needsClasses = 0;       // bitmask of Vehicles::VehicleClass; one of them must exist in the build (boats, aircraft)
    bool hidden = false;        // started by script (phone/trigger), no start blip
};

// Mission test context (--missiontest): scripted player actions used by Mission::autotest.
struct MissionTest {
    GameWorld* g = nullptr;
    bool active = false;
    float stageTime = 0.f;      // time since the mission's current stage began
    int lastStage = -1;
    float missionTime = 0.f;
    int shots = 0;
    std::string id;
    // actions
    void teleport(vec3 p, float yaw);            // player (and the vehicle the player drives)
    void teleportNear(vec2 p, float dist);       // next to p, on the ground / road
    void enter(int veh, int seat = 0);
    void exitVehicle();
    void killEnemies(float radius = 1e9f);       // all living FAC_ENEMY mission peds
    void destroy(int veh);
    void shoot(int ped);                         // kill one ped as the player
    void driveToward(vec2 p, float speed, float dt);   // move the player's vehicle along a road route (teleport steps)
    void stopVehicle();
    void screenshot(const char* tag);
    void log(const char* fmt, ...);
    std::vector<vec2> route;
    vec2 routeGoal = vec2(1e9f, 1e9f);
};

// Checkpoint restarted by the retry prompt after a failure.
struct RetryState {
    int def = -1;
    int checkpoint = 0;
    float timer = 0.f;          // prompt visible while > 0
    bool pending = false;       // waiting for the player to respawn
    // loadout at mission start (restored on retry)
    bool hasWeapon[WPN_COUNT] = {};
    int ammo[WPN_COUNT] = {};
    float armor = 0.f;
    int weapon = 0;
    long long money = 0;
    bool replay = false;
};

struct MissionManager {
    std::vector<MissionDef> defs;
    Mission* active = nullptr;
    int activeDef = -1;
    // mission-owned entities (cleaned up on end)
    std::vector<int> peds, vehicles;
    std::vector<Marker> markers;
    // dialogue queue
    std::vector<DialogueLine> lines;
    float lineTimer = 0.f;
    Audio::SoundHandle lineSound = 0;
    // cutscene
    std::vector<CutsceneShot> shots;
    int shotIndex = -1;
    float shotTime = 0.f;
    bool skippable = true;
    bool holdForDialogue = false;   // story cutscenes keep framing the speakers until the conversation ends
    int autoShots = 0;              // speaker shots added to the current cutscene
    // result banner
    float passTimer = 0.f;
    float cooldown = 0.f;
    float failTimer = 0.f;
    // registered content
    void registerAll(GameWorld& g);
    // Additions:
    int checkpoint = 0;         // last checkpoint reached by the active mission
    int startCheckpoint = 0;    // checkpoint for the next startMission() (retry)
    bool replay = false;        // replaying a finished story mission from the phone (no rewards, flags untouched)
    RetryState retry;
    bool suppressPolice = false;        // active mission keeps the wanted level at zero
    bool allowSwitch = false;           // character switching allowed during the active mission
    int overlayFrames = 0;              // incremented by drawMissionOverlay (detects the app hook)
    int updateFrames = 0;
    float lastOverlaySeen = -1.f;
    MissionTest test;
    int findDef(const char* id) const;
};

extern MissionManager gMissions;

// storyFlags layout (saved with the game): [0,64) story mission flags, [64,128) side-activity completion flags,
// [128, kFlagCount) extended integer state of the open world (economy, collectibles, switching, records).
enum StoryFlagLayout : int {
    kSideBase = 64,
    kExtBase = 128,
    kFlagCount = 512,
};

// Open world systems (defined by the story/economy modules, called from the mission runtime).
void updateOpenWorld(GameWorld& g, float dt);
void openWorldOnMissionEnd(GameWorld& g, int def, bool passed);
bool openWorldBusy();                    // a shop/phone/menu has the player's attention
// Draws mission/open-world 2D overlays (shop and phone menus, race HUD, choices). Called by the app between
// UI::drawHud and UI::endFrame; when the app does not call it, menus fall back to the HUD help box.
void drawMissionOverlay(GameWorld& g, float dt);
std::string speakableText(const std::string& text);   // phonetic spellings of story names for the TTS
void drawMarkers(GameWorld& g, const std::vector<Marker>& list);   // glowing cylinder markers

}  // namespace Game
