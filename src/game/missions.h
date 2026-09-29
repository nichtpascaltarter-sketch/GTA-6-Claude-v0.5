// Mission framework: scripted story missions and side activities as small state machines driven by helper calls on
// GameWorld (objectives, targets with GPS, markers, dialogue with TTS + subtitles, cutscene camera shots, mission
// entities that are cleaned up automatically, pass/fail with rewards).
#pragma once
#include "gameworld.h"

namespace Game {

enum MissionStatus : u8 { MS_RUNNING = 0, MS_PASSED, MS_FAILED };

struct DialogueLine {
    std::string speaker;     // subtitle name
    std::string text;        // spoken + shown
    int ped = -1;            // speaking ped (positional voice); -1 = use voice preset below (radio/phone)
    bool female = false;     // preset voice gender when ped < 0
    u32 voiceSeed = 1;
    float pause = 0.25f;     // silence after the line
    u32 color = 0xff66ccff;  // speaker name color
};

struct CutsceneShot {
    dvec3 pos, target;
    dvec3 pos2, target2;     // camera moves linearly from pos/target to pos2/target2 over the shot
    float fov = 50.f;
    float duration = 3.f;
};

struct Marker {
    dvec3 pos;
    float radius = 2.f;
    vec3 color = vec3(1.f, 0.85f, 0.2f);
    bool vehicleSized = false;
};

class Mission {
public:
    virtual ~Mission() {}
    virtual const char* title() const = 0;
    virtual const char* brief() const = 0;   // pause-menu mission log text
    virtual void start(GameWorld& g) = 0;
    virtual MissionStatus update(GameWorld& g, float dt) = 0;
    virtual long long reward() const { return 0; }
    std::string failReason;
    int stage = 0;
    float stageTime = 0.f;
    void next() {
        stage++;
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
    // result banner
    float passTimer = 0.f;
    float cooldown = 0.f;
    float failTimer = 0.f;
    // registered content
    void registerAll(GameWorld& g);
};

extern MissionManager gMissions;

}  // namespace Game
