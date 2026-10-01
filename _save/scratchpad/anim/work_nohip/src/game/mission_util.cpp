// Mission scripting library shared by the story, side activities and the open world: story locations resolved on the
// road network, the story cast (named characters with their own looks and voices), spawn helpers, dialogue and phone
// call helpers, cutscene shot builders, the adaptive score and small queries used by mission state machines.
#include "missions.h"

namespace Game {
namespace mu {

// ------------------------------------------------------------------------------------------------------------------
// Speaker colors (RGBA8, r in the low byte)
const u32 kColMari = 0xffcc55ffu;
const u32 kColDex = 0xff66ddaau;
const u32 kColTomas = 0xff55ccffu;
const u32 kColLucha = 0xff7fb4ffu;
const u32 kColRook = 0xff40a0e0u;
const u32 kColKit = 0xfff0c040u;
const u32 kColJonah = 0xff60c080u;
const u32 kColSandoval = 0xffd0d0d0u;
const u32 kColHolt = 0xffe08060u;
const u32 kColCuervo = 0xff4040ffu;
const u32 kColThug = 0xff6060e0u;
const u32 kColOther = 0xffb0b0b0u;
const u32 kColSable = 0xffd8b0e8u;
const u32 kColAgent = 0xffe0d070u;

// ------------------------------------------------------------------------------------------------------------------
// Story cast
enum CastId : int {
    CAST_TOMAS = 0, CAST_LUCHA, CAST_ROOK, CAST_KIT, CAST_JONAH, CAST_SANDOVAL, CAST_HOLT, CAST_CUERVO,
    CAST_THUG_A, CAST_THUG_B, CAST_THUG_C, CAST_THUG_D,      // Cuervo gang members (black and red)
    CAST_GUARD_A, CAST_GUARD_B,                              // Sandoval's private security (dark suits)
    CAST_COP_A, CAST_COP_B,                                  // Holt's officers
    CAST_BOUNCER, CAST_DOCKER, CAST_REPORTER, CAST_BANKER, CAST_PILOT, CAST_MECHANIC,
    // Act 4 (story_act4.cpp): Ines Sable of Sable Maritime, her accountant, her courier, the state task force agent
    CAST_SABLE, CAST_PRUITT, CAST_VOSS, CAST_AGENT,
    CAST_COUNT
};

struct CastInfo {
    const char* key;
    const char* name;
    u32 color;
};

const CastInfo kCast[CAST_COUNT] = {
    {"cast_tomas", "Tomas", kColTomas},       {"cast_lucha", "Lucha", kColLucha},
    {"cast_rook", "Rook", kColRook},          {"cast_kit", "Kit", kColKit},
    {"cast_jonah", "Jonah", kColJonah},       {"cast_sandoval", "Sandoval", kColSandoval},
    {"cast_holt", "Holt", kColHolt},          {"cast_cuervo", "El Cuervo", kColCuervo},
    {"cast_thug_a", "Cuervo", kColThug},      {"cast_thug_b", "Cuervo", kColThug},
    {"cast_thug_c", "Cuervo", kColThug},      {"cast_thug_d", "Cuervo", kColThug},
    {"cast_guard_a", "Guard", kColOther},     {"cast_guard_b", "Guard", kColOther},
    {"cast_cop_a", "Officer", kColHolt},      {"cast_cop_b", "Officer", kColHolt},
    {"cast_bouncer", "Bouncer", kColOther},   {"cast_docker", "Dock Worker", kColOther},
    {"cast_reporter", "Reporter", kColOther}, {"cast_banker", "Banker", kColOther},
    {"cast_pilot", "Pilot", kColOther},       {"cast_mechanic", "Mechanic", kColOther},
    {"cast_sable", "Sable", kColSable},       {"cast_pruitt", "Pruitt", kColOther},
    {"cast_voss", "Voss", kColOther},         {"cast_agent", "Agent Dela Cruz", kColAgent},
};

vec3 lin(float r, float g, float b) { return srgbToLinear(vec3(r, g, b)); }

// CharacterDesc conventions: see anim/character.cpp (hairStyle/top/bottom/shoes/hat/glasses/facialHair indices).
Anim::CharacterDesc castDesc(int id) {
    Anim::CharacterDesc d;
    d.seed = 0xC0FFEEu + (u32)id * 7919u;
    switch (id) {
        case CAST_TOMAS:   // 22, wiry, curly hair, oversized yellow tee, baggy jeans, backwards cap
            d.gender = Anim::MALE; d.height = 1.74f; d.weight = 0.3f; d.muscle = 0.3f; d.age = 0.04f;
            d.skinTone = vec3(0.6f, 0.42f, 0.31f); d.hairStyle = 3; d.hairColor = vec3(0.04f, 0.03f, 0.025f);
            d.top = 16; d.topColor = lin(0.95f, 0.78f, 0.2f); d.bottom = 8; d.bottomColor = lin(0.35f, 0.4f, 0.55f);
            d.shoes = 0; d.shoeColor = lin(0.95f, 0.95f, 0.95f); d.hat = 1; break;
        case CAST_LUCHA:   // 63, short and sturdy, grey bun, red blouse, dark skirt, reading glasses
            d.gender = Anim::FEMALE; d.height = 1.57f; d.weight = 0.78f; d.muscle = 0.35f; d.age = 0.75f;
            d.skinTone = vec3(0.52f, 0.36f, 0.26f); d.hairStyle = 6; d.hairColor = vec3(0.42f, 0.42f, 0.42f);
            d.top = 13; d.topColor = lin(0.78f, 0.16f, 0.2f); d.bottom = 4; d.bottomColor = lin(0.12f, 0.12f, 0.16f);
            d.shoes = 5; d.shoeColor = lin(0.15f, 0.1f, 0.08f); d.glasses = 2; break;
        case CAST_ROOK:    // 52, big, buzz cut, grey boxed beard, dark hawaiian shirt, work pants, boots, aviators
            d.gender = Anim::MALE; d.height = 1.9f; d.weight = 0.72f; d.muscle = 0.55f; d.age = 0.55f;
            d.skinTone = vec3(0.2f, 0.13f, 0.09f); d.hairStyle = 1; d.hairColor = vec3(0.3f, 0.3f, 0.3f); d.facialHair = 4;
            d.top = 3; d.topColor = lin(0.12f, 0.3f, 0.22f); d.bottom = 10; d.bottomColor = lin(0.3f, 0.28f, 0.22f);
            d.shoes = 2; d.shoeColor = lin(0.3f, 0.2f, 0.12f); d.glasses = 1; break;
        case CAST_KIT:     // 26, teal bob, purple hoodie, black leggings, running shoes
            d.gender = Anim::FEMALE; d.height = 1.63f; d.weight = 0.25f; d.muscle = 0.25f; d.age = 0.1f;
            d.skinTone = vec3(0.78f, 0.6f, 0.47f); d.hairStyle = 9; d.hairColor = lin(0.1f, 0.62f, 0.6f);
            d.top = 5; d.topColor = lin(0.42f, 0.18f, 0.62f); d.bottom = 9; d.bottomColor = lin(0.06f, 0.06f, 0.07f);
            d.shoes = 6; d.shoeColor = lin(0.95f, 0.4f, 0.6f); break;
        case CAST_JONAH:   // 60, lean, long grey hair, full beard, khaki shirt, work pants, boots, cap
            d.gender = Anim::MALE; d.height = 1.81f; d.weight = 0.3f; d.muscle = 0.45f; d.age = 0.62f;
            d.skinTone = vec3(0.74f, 0.52f, 0.37f); d.hairStyle = 5; d.hairColor = vec3(0.5f, 0.48f, 0.44f); d.facialHair = 3;
            d.top = 4; d.topColor = lin(0.62f, 0.56f, 0.4f); d.bottom = 10; d.bottomColor = lin(0.26f, 0.3f, 0.2f);
            d.shoes = 2; d.shoeColor = lin(0.25f, 0.17f, 0.1f); d.hat = 0; d.role = 5; break;
        case CAST_SANDOVAL:  // 56, slicked salt-and-pepper hair, cream suit, aviators
            d.gender = Anim::MALE; d.height = 1.84f; d.weight = 0.45f; d.muscle = 0.35f; d.age = 0.58f;
            d.skinTone = vec3(0.8f, 0.62f, 0.5f); d.hairStyle = 8; d.hairColor = vec3(0.28f, 0.27f, 0.26f);
            d.top = 6; d.topColor = lin(0.88f, 0.85f, 0.78f); d.bottom = 3; d.bottomColor = lin(0.85f, 0.82f, 0.75f);
            d.shoes = 1; d.shoeColor = lin(0.35f, 0.2f, 0.1f); d.glasses = 1; d.role = 3; break;
        case CAST_HOLT:    // 48, blonde bun, police captain uniform
            d.gender = Anim::FEMALE; d.height = 1.73f; d.weight = 0.42f; d.muscle = 0.55f; d.age = 0.45f;
            d.skinTone = vec3(0.86f, 0.68f, 0.56f); d.hairStyle = 6; d.hairColor = vec3(0.52f, 0.42f, 0.26f);
            d.top = 7; d.topColor = lin(0.12f, 0.16f, 0.3f); d.bottom = 7; d.bottomColor = lin(0.1f, 0.12f, 0.22f);
            d.shoes = 2; d.shoeColor = vec3(0.02f); d.role = 1; break;
        case CAST_CUERVO:  // 35, muscular, buzz cut, goatee, black tank top, red bandana, sunglasses
            d.gender = Anim::MALE; d.height = 1.79f; d.weight = 0.5f; d.muscle = 0.8f; d.age = 0.3f;
            d.skinTone = vec3(0.5f, 0.34f, 0.24f); d.hairStyle = 1; d.hairColor = vec3(0.03f); d.facialHair = 2;
            d.top = 1; d.topColor = lin(0.06f, 0.06f, 0.07f); d.bottom = 8; d.bottomColor = lin(0.1f, 0.1f, 0.12f);
            d.shoes = 0; d.shoeColor = lin(0.08f, 0.08f, 0.08f); d.hat = 7; d.glasses = 0; d.role = 2; break;
        case CAST_THUG_A: case CAST_THUG_B: case CAST_THUG_C: case CAST_THUG_D: {
            int k = id - CAST_THUG_A;
            d.gender = k == 3 ? Anim::FEMALE : Anim::MALE;
            d.height = 1.72f + 0.05f * (k % 3); d.weight = 0.35f + 0.12f * k; d.muscle = 0.5f + 0.1f * (k & 1); d.age = 0.12f + 0.06f * k;
            const vec3 skins[4] = {vec3(0.55f, 0.38f, 0.27f), vec3(0.42f, 0.28f, 0.19f), vec3(0.7f, 0.52f, 0.4f), vec3(0.6f, 0.43f, 0.31f)};
            d.skinTone = skins[k];
            const int hair[4] = {2, 1, 10, 4};
            d.hairStyle = hair[k];
            d.hairColor = vec3(0.03f);
            const int tops[4] = {0, 5, 1, 15};
            d.top = tops[k];
            d.topColor = (k & 1) ? lin(0.7f, 0.08f, 0.1f) : lin(0.07f, 0.07f, 0.08f);
            d.bottom = k == 3 ? 9 : 8;
            d.bottomColor = lin(0.1f, 0.1f, 0.12f);
            d.shoes = 0; d.shoeColor = (k & 1) ? lin(0.08f, 0.08f, 0.08f) : lin(0.75f, 0.1f, 0.1f);
            const int hats[4] = {7, -1, 1, 7};
            d.hat = hats[k];
            d.facialHair = k == 1 ? 0 : (k == 2 ? 2 : -1);
            d.role = 2;
            break;
        }
        case CAST_GUARD_A: case CAST_GUARD_B: {
            int k = id - CAST_GUARD_A;
            d.gender = Anim::MALE; d.height = 1.86f + 0.03f * k; d.weight = 0.6f; d.muscle = 0.75f; d.age = 0.3f + 0.1f * k;
            d.skinTone = k ? vec3(0.3f, 0.2f, 0.14f) : vec3(0.82f, 0.65f, 0.52f); d.hairStyle = k ? 1 : 2; d.hairColor = vec3(0.05f);
            d.top = 6; d.topColor = lin(0.07f, 0.07f, 0.08f); d.bottom = 3; d.bottomColor = lin(0.07f, 0.07f, 0.08f);
            d.shoes = 1; d.shoeColor = vec3(0.02f); d.glasses = 0; d.role = 3;
            break;
        }
        case CAST_COP_A: case CAST_COP_B: {
            int k = id - CAST_COP_A;
            d.gender = Anim::MALE; d.height = 1.8f; d.weight = 0.55f + 0.1f * k; d.muscle = 0.6f; d.age = 0.35f + 0.1f * k;
            d.skinTone = k ? vec3(0.62f, 0.44f, 0.32f) : vec3(0.86f, 0.7f, 0.58f); d.hairStyle = 1; d.hairColor = vec3(0.1f, 0.07f, 0.05f);
            d.top = 7; d.topColor = lin(0.12f, 0.16f, 0.3f); d.bottom = 7; d.bottomColor = lin(0.1f, 0.12f, 0.22f);
            d.shoes = 2; d.shoeColor = vec3(0.02f); d.hat = 2; d.glasses = k ? 1 : -1; d.facialHair = k ? 1 : -1; d.role = 1;
            break;
        }
        case CAST_BOUNCER:
            d.gender = Anim::MALE; d.height = 1.95f; d.weight = 0.8f; d.muscle = 0.9f; d.age = 0.35f;
            d.skinTone = vec3(0.24f, 0.16f, 0.11f); d.hairStyle = 0; d.facialHair = 4; d.hairColor = vec3(0.03f);
            d.top = 0; d.topColor = lin(0.05f, 0.05f, 0.06f); d.bottom = 3; d.bottomColor = lin(0.05f, 0.05f, 0.06f);
            d.shoes = 1; d.shoeColor = vec3(0.02f); d.role = 3; break;
        case CAST_DOCKER:
            d.gender = Anim::MALE; d.height = 1.78f; d.weight = 0.6f; d.muscle = 0.65f; d.age = 0.45f;
            d.skinTone = vec3(0.7f, 0.5f, 0.38f); d.hairStyle = 2; d.hairColor = vec3(0.2f, 0.14f, 0.08f); d.facialHair = 0;
            d.top = 9; d.topColor = lin(0.95f, 0.55f, 0.1f); d.bottom = 10; d.bottomColor = lin(0.2f, 0.24f, 0.32f);
            d.shoes = 2; d.shoeColor = lin(0.3f, 0.2f, 0.1f); d.hat = 3; d.role = 5; break;
        case CAST_REPORTER:
            d.gender = Anim::FEMALE; d.height = 1.7f; d.weight = 0.35f; d.muscle = 0.3f; d.age = 0.3f;
            d.skinTone = vec3(0.45f, 0.3f, 0.21f); d.hairStyle = 5; d.hairColor = vec3(0.05f, 0.03f, 0.02f);
            d.top = 13; d.topColor = lin(0.2f, 0.45f, 0.75f); d.bottom = 3; d.bottomColor = lin(0.15f, 0.15f, 0.18f);
            d.shoes = 5; d.shoeColor = vec3(0.02f); d.role = 3; break;
        case CAST_BANKER:
            d.gender = Anim::MALE; d.height = 1.76f; d.weight = 0.55f; d.muscle = 0.2f; d.age = 0.5f;
            d.skinTone = vec3(0.88f, 0.72f, 0.6f); d.hairStyle = 10; d.hairColor = vec3(0.35f, 0.3f, 0.25f); d.glasses = 2;
            d.top = 6; d.topColor = lin(0.2f, 0.22f, 0.3f); d.bottom = 3; d.bottomColor = lin(0.2f, 0.22f, 0.3f);
            d.shoes = 1; d.shoeColor = vec3(0.03f); d.role = 3; break;
        case CAST_PILOT:
            d.gender = Anim::FEMALE; d.height = 1.69f; d.weight = 0.35f; d.muscle = 0.45f; d.age = 0.32f;
            d.skinTone = vec3(0.66f, 0.47f, 0.34f); d.hairStyle = 4; d.hairColor = vec3(0.12f, 0.08f, 0.05f); d.glasses = 1;
            d.top = 4; d.topColor = lin(0.9f, 0.9f, 0.92f); d.bottom = 3; d.bottomColor = lin(0.12f, 0.14f, 0.2f);
            d.shoes = 1; d.shoeColor = vec3(0.02f); d.role = 3; break;
        case CAST_SABLE:   // 54, tall and spare, silver bob, charcoal suit, no jewellery but a watch
            d.gender = Anim::FEMALE; d.height = 1.74f; d.weight = 0.28f; d.muscle = 0.3f; d.age = 0.56f;
            d.skinTone = vec3(0.86f, 0.7f, 0.6f); d.ancestry = 2; d.hairStyle = 9; d.hairColor = vec3(0.7f, 0.7f, 0.72f);
            d.top = 6; d.topColor = lin(0.13f, 0.13f, 0.15f); d.bottom = 3; d.bottomColor = lin(0.13f, 0.13f, 0.15f);
            d.shoes = 1; d.shoeColor = vec3(0.02f); d.role = 3; break;
        case CAST_PRUITT:  // 47, soft, thinning hair, reading glasses, pale blue shirt, a tote he never puts down
            d.gender = Anim::MALE; d.height = 1.71f; d.weight = 0.66f; d.muscle = 0.2f; d.age = 0.47f;
            d.skinTone = vec3(0.88f, 0.72f, 0.62f); d.hairStyle = 10; d.hairColor = vec3(0.42f, 0.34f, 0.26f); d.glasses = 2;
            d.top = 4; d.topColor = lin(0.62f, 0.72f, 0.86f); d.bottom = 3; d.bottomColor = lin(0.35f, 0.36f, 0.4f);
            d.shoes = 1; d.shoeColor = lin(0.25f, 0.15f, 0.08f); d.bag = 2; d.bagColor = lin(0.15f, 0.12f, 0.1f); d.role = 3; break;
        case CAST_VOSS:    // 40, big, shaved head, short beard, black jacket over a dark shirt
            d.gender = Anim::MALE; d.height = 1.9f; d.weight = 0.6f; d.muscle = 0.75f; d.age = 0.38f;
            d.skinTone = vec3(0.3f, 0.2f, 0.14f); d.hairStyle = 0; d.facialHair = 4; d.hairColor = vec3(0.03f);
            d.top = 3; d.topColor = lin(0.1f, 0.12f, 0.16f); d.outer = 3; d.outerColor = lin(0.04f, 0.04f, 0.05f);
            d.bottom = 3; d.bottomColor = lin(0.08f, 0.08f, 0.09f); d.shoes = 1; d.shoeColor = vec3(0.02f); d.role = 2; break;
        case CAST_AGENT:   // 44, dark bun, white shirt, navy blazer and slacks, a badge on the belt
            d.gender = Anim::FEMALE; d.height = 1.68f; d.weight = 0.4f; d.muscle = 0.5f; d.age = 0.44f;
            d.skinTone = vec3(0.6f, 0.43f, 0.31f); d.ancestry = 0; d.hairStyle = 6; d.hairColor = vec3(0.04f, 0.03f, 0.02f);
            d.top = 4; d.topColor = lin(0.93f, 0.93f, 0.92f); d.outer = 5; d.outerColor = lin(0.08f, 0.1f, 0.2f);
            d.bottom = 3; d.bottomColor = lin(0.08f, 0.1f, 0.2f); d.shoes = 1; d.shoeColor = vec3(0.02f); d.role = 3; break;
        case CAST_MECHANIC:
        default:
            d.gender = Anim::MALE; d.height = 1.75f; d.weight = 0.5f; d.muscle = 0.5f; d.age = 0.4f;
            d.skinTone = vec3(0.58f, 0.4f, 0.28f); d.hairStyle = 2; d.hairColor = vec3(0.05f); d.facialHair = 1;
            d.top = 1; d.topColor = lin(0.85f, 0.85f, 0.85f); d.bottom = 10; d.bottomColor = lin(0.2f, 0.25f, 0.4f);
            d.shoes = 2; d.shoeColor = lin(0.2f, 0.15f, 0.1f); d.hat = 0; d.role = 5; break;
    }
    return d;
}

// Voices come from the speech module's personas (timbre plus default accent, delivery and mood); the persona's style
// tags lead every spoken line so the accent carries over, and inline tags in a line ([angry], [whisper]...) override it.
// The Act 4 cast has no stock persona: their voices and styles are set here.
Audio::VoiceParams extraCastVoice(int id) {
    Audio::VoiceParams v;
    switch (id) {
        case CAST_SABLE: v.pitch = 168.f; v.formantScale = 1.1f; v.speed = 0.9f; v.breathiness = 0.1f; v.roughness = 0.06f; v.expressiveness = 0.7f; break;
        case CAST_PRUITT: v.pitch = 132.f; v.formantScale = 1.03f; v.speed = 1.12f; v.breathiness = 0.16f; v.roughness = 0.02f; v.expressiveness = 1.25f; break;
        case CAST_VOSS: v.pitch = 92.f; v.formantScale = 0.94f; v.speed = 0.94f; v.breathiness = 0.08f; v.roughness = 0.3f; v.expressiveness = 0.85f; break;
        case CAST_AGENT:
        default: v.pitch = 186.f; v.formantScale = 1.12f; v.speed = 1.02f; v.breathiness = 0.1f; v.roughness = 0.04f; v.expressiveness = 0.95f; break;
    }
    return v;
}
std::string extraCastTags(int id) {
    switch (id) {
        case CAST_SABLE: return "[accent:british:0.5][dark]";
        case CAST_PRUITT: return "[nasal]";
        case CAST_VOSS: return "[accent:caribbean:0.45][gravelly]";
        default: return "[accent:latino:0.3]";
    }
}
Audio::VoiceParams castVoice(int id) { return id >= CAST_SABLE ? extraCastVoice(id) : Speech::persona(kCast[id].key).voice; }
std::string castTags(int id) { return id >= CAST_SABLE ? extraCastTags(id) : Speech::persona(kCast[id].key).tags(); }
const char* protagonistKey(int who) { return who == 0 ? "mari" : "dex"; }
Audio::VoiceParams protagonistVoice(int who) { return Speech::persona(protagonistKey(who)).voice; }
std::string protagonistTags(int who) { return Speech::persona(protagonistKey(who)).tags(); }

int castChar(GameWorld& g, int id) { return g.namedCharacter(kCast[id].key, castDesc(id)); }

// Spawns a cast member as a mission ped (auto cleanup) with the cast voice.
int spawnCast(GameWorld& g, int id, vec3 pos, float yaw, Faction f = FAC_FRIEND) {
    int ci = castChar(g, id);
    int p = g.mPed(ci, dvec3(pos), yaw, f);
    if (p >= 0) {
        g.peds[p].voice = castVoice(id);
        g.peds[p].brain.type = BRAIN_NONE;
    }
    return p;
}

// ------------------------------------------------------------------------------------------------------------------
// Locations
float groundAt(GameWorld& g, float x, float y, float zRef = 200.f) {
    float z = g.groundHeight(x, y, zRef);
    if (z < -1e6f) z = g.map->heightAt(x, y);
    return z;
}

bool driveableClass(const World::RoadEdge& e) {
    if (e.flags & (World::RF_ELEVATED)) return false;
    return e.cls != World::RC_HIGHWAY && e.cls != World::RC_RAMP;
}

// Nearest local street edge (no highways/ramps/elevated; bridges optional) within maxDist.
int nearestStreet(GameWorld& g, vec2 p, float maxDist, float* sOut, float* sideOut, bool allowBridge = false) {
    std::vector<int> cand;
    g.roads->edgesInRect(p - vec2(maxDist), p + vec2(maxDist), cand);
    int best = -1;
    float bestD = maxDist, bestS = 0.f, bestSide = 1.f;
    for (int ei : cand) {
        const World::RoadEdge& e = g.roads->edges[ei];
        if (!driveableClass(e)) continue;
        if (!allowBridge && (e.flags & World::RF_BRIDGE)) continue;
        float acc = 0.f;
        for (size_t k = 0; k + 1 < e.pts.size(); k++) {
            vec2 a = e.pts[k].xy(), b = e.pts[k + 1].xy();
            float t;
            float d = distPointSegment2D(p, a, b, &t);
            float seg = length(b - a);
            if (d < bestD) {
                bestD = d;
                best = ei;
                bestS = acc + t * seg;
                vec2 dir = normalize(b - a);
                bestSide = cross(dir, p - a) >= 0.f ? 1.f : -1.f;   // +1 = left of travel direction
            }
            acc += seg;
        }
    }
    if (best < 0) return g.roads->nearestEdge(p, maxDist * 2.f, sOut, nullptr, sideOut);
    if (sOut) *sOut = bestS;
    if (sideOut) *sideOut = bestSide;
    return best;
}

struct Place {
    vec2 hint;
    vec3 pos;        // on the sidewalk (on-foot marker), facing the street direction
    float yaw = 0.f;
    vec3 curb;       // parking spot at the curb next to pos (right side of travel)
    float curbYaw = 0.f;
    vec3 door;       // a few meters from pos, away from the street (where people come out of buildings)
    vec2 streetDir;  // unit direction of the street
    vec2 outward;    // unit vector from the street toward the sidewalk
    int edge = -1;
    float s = 0.f;   // distance along the edge
    float side = 1.f;  // +1 when the place is left of the edge direction
};

Place resolvePlace(GameWorld& g, vec2 hint, float along = 0.f, bool allowBridge = false) {
    Place pl;
    pl.hint = hint;
    float s = 0.f, side = 1.f;
    int e = nearestStreet(g, hint, 260.f, &s, &side, allowBridge);
    pl.edge = e;
    if (e < 0) {
        float z = groundAt(g, hint.x, hint.y);
        pl.pos = pl.curb = pl.door = vec3(hint, z);
        pl.streetDir = vec2(0, 1);
        pl.outward = vec2(1, 0);
        return pl;
    }
    const World::RoadEdge& ed = g.roads->edges[e];
    float lo = Min(ed.cut0 + 3.f, ed.length * 0.5f), hi = Max(ed.length - ed.cut1 - 3.f, ed.length * 0.5f);
    s = Clamp(s + along, lo, hi);
    pl.s = s;
    pl.side = side;
    vec3 c = ed.posAt(s);
    vec3 t = ed.tangentAt(s);
    vec2 dir = normalize(vec2(t.x, t.y));
    vec2 left = perp(dir);
    vec2 out = left * side;   // toward the hint's side of the street
    pl.streetDir = dir;
    pl.outward = out;
    float walk = ed.halfWidth + Max(ed.sidewalk, 1.2f) * 0.5f;
    vec2 sp = c.xy() + out * walk;
    pl.pos = vec3(sp, groundAt(g, sp.x, sp.y, c.z + 3.f));
    // curb: on the same side, in the parking/shoulder strip; heading so that the curb is on the right
    const World::RoadClassInfo& info = World::roadInfo(ed.cls);
    vec2 cp = c.xy() + out * (ed.halfWidth - Max(info.shoulder, 1.2f) * 0.5f - 0.4f);
    pl.curb = vec3(cp, groundAt(g, cp.x, cp.y, c.z + 3.f));
    // right side of travel = -left; if out == left the travel direction is -dir
    vec2 travel = side > 0.f ? -dir : dir;
    pl.curbYaw = atan2f(-travel.x, travel.y);
    pl.yaw = atan2f(-dir.x, dir.y);
    vec2 dp = sp + out * 4.5f;
    pl.door = vec3(dp, groundAt(g, dp.x, dp.y, c.z + 3.f));
    return pl;
}

// True when the place's door opens into a building: the door spot is outside, a facade stands right behind it.
bool hasFrontage(GameWorld& g, const Place& p) {
    if (p.edge < 0 || !g.buildings) return false;
    if (g.buildings->pointInBuilding(p.door.xy(), 0.3f)) return false;
    for (float d = 1.5f; d <= 7.5f; d += 1.5f)
        if (g.buildings->pointInBuilding(p.door.xy() + p.outward * d, 0.f)) return true;
    return false;
}

// A street place in front of a building (shops, homes, offices): the nearest one to the hint whose door opens into a
// facade. Hints are only rough (the city is generated), so this keeps story locations off empty lots.
Place resolveFrontage(GameWorld& g, vec2 hint, float along = 0.f) {
    Place base = resolvePlace(g, hint, along);
    if (hasFrontage(g, base)) return base;
    Place best = base;
    float bestD = 1e9f;
    for (float r = 10.f; r <= 220.f && bestD > 1e8f; r += 10.f) {
        int n = Max(6, (int)(r * kTwoPi / 12.f));
        for (int i = 0; i < n; i++) {
            float a = kTwoPi * i / n;
            Place p = resolvePlace(g, hint + vec2(cosf(a), sinf(a)) * r);
            if (!hasFrontage(g, p)) continue;
            float d = ::length(p.pos.xy() - hint);
            if (d < bestD) {
                bestD = d;
                best = p;
            }
        }
    }
    if (bestD > 1e8f) LOG("Story place near (%.0f, %.0f): no building frontage found, using the street", hint.x, hint.y);
    return best;
}

// A place at an interior's front door (world/interiors.h: the planner puts enterable interiors into real buildings),
// snapped to the street in front of it. `name` binds a named interior (shops), otherwise the nth interior of `kind`.
bool placeFromInterior(GameWorld& g, const char* name, int kind, int nth, Place& out) {
    if (!World::gInteriors) return false;
    int i = name ? World::gInteriors->byName(name) : World::gInteriors->byKind((u8)kind, nth);
    if (i < 0) return false;
    const World::InteriorDef& d = World::gInteriors->defs[i];
    const World::InteriorMarker* m = d.marker(World::IM_DOOR_OUT);
    vec3 door = m ? d.toWorld(m->pos) : d.toWorld(vec3(0.f, -2.f, 0.f));
    Place p = resolvePlace(g, door.xy());
    if (p.edge < 0) return false;
    p.door = vec3(door.xy(), groundAt(g, door.x, door.y, door.z + 2.f));
    out = p;
    return true;
}

// Building places: the interior's entrance when the world has one, else the nearest real frontage to the hint.
Place resolveBuilding(GameWorld& g, vec2 hint, int kind, const char* name = nullptr) {
    Place p;
    if (placeFromInterior(g, name, kind, 0, p)) return p;
    return resolveFrontage(g, hint);
}

// ------------------------------------------------------------------------------------------------------------------
// Staging scenes inside the enterable interiors (the diner, Rook's garage...): points in the interior's own frame (x
// along the frontage, y inward from the entrance, z up from the floor) become world points. Nothing is staged when the
// world has no interior of that name; the missions keep their street scenes then.
struct InteriorStage {
    const World::InteriorDef* d = nullptr;
    bool ok() const { return d != nullptr; }
    vec3 at(vec3 local) const { return d->toWorld(local); }
    float yaw(float localYaw) const { return d->yawToWorld(localYaw); }
    bool marker(u8 kind, vec3& local, float* localYaw = nullptr) const {
        const World::InteriorMarker* m = d ? d->marker(kind) : nullptr;
        if (!m) return false;
        local = m->pos;
        if (localYaw) *localYaw = m->yaw;
        return true;
    }
    // the nth scenario spot of a role (behind the counter, by the lift...)
    bool scenario(u8 role, int nth, vec3& local, float* localYaw = nullptr) const {
        if (!d) return false;
        for (const World::InteriorScenario& sc : d->scenarios)
            if (sc.role == role && nth-- == 0) {
                local = sc.pos;
                if (localYaw) *localYaw = sc.yaw;
                return true;
            }
        return false;
    }
    // just inside the entrance
    vec3 entry() const {
        vec3 p;
        if (marker(World::IM_ENTRY, p)) return p;
        if (marker(World::IM_DOOR_OUT, p)) return vec3(p.x, 1.2f, p.z);
        return vec3((d->x0 + d->x1) * 0.5f, 1.2f, 0.f);
    }
};

InteriorStage interiorStage(const char* name) {
    InteriorStage s;
    if (World::gInteriors) {
        int i = World::gInteriors->byName(name);
        if (i >= 0) s.d = &World::gInteriors->defs[i];
    }
    return s;
}

// Deep enough water near hint (spiral search), z at the water surface.
bool findWater(GameWorld& g, vec2 hint, float minDepth, vec3& out, float maxR = 900.f) {
    for (float r = 0.f; r <= maxR; r += 12.f) {
        int n = r < 1.f ? 1 : (int)(r * kTwoPi / 12.f);
        for (int i = 0; i < n; i++) {
            float a = kTwoPi * i / n;
            vec2 p = hint + vec2(cosf(a), sinf(a)) * r;
            float w = g.map->waterAt(p.x, p.y);
            if (w <= World::kNoWater + 1.f) continue;
            float h = g.map->heightAt(p.x, p.y);
            if (w - h < minDepth) continue;
            out = vec3(p, w);
            return true;
        }
    }
    out = vec3(hint, 0.f);
    return false;
}

bool isWaterAt(GameWorld& g, vec2 p, float minDepth) {
    float w = g.map->waterAt(p.x, p.y);
    return w > World::kNoWater + 1.f && w - g.map->heightAt(p.x, p.y) >= minDepth;
}

// All story locations, resolved once per world. Hints follow the district layout (worldmap.cpp) and the landmark
// sites (world/sites.cpp: Solaris One plaza, Sol Beach Pier, Port Isle terminal, airport helipad, Sawgrass dock).
struct Places {
    bool ready = false;
    Place mariApt, boatyard, diner, vargasGarage, stashHouse, solarisPier, rookShop, dexTrailer, pulseFm, policeHq;
    Place solarisOne, sandovalOffice, palmMotors, gunFlats, gunNorth, resprayCL, resprayBeach, threads, clubRiptide;
    Place beachCondo, beachPier, portGate, keyCoral, keyCoralMarina, sandovalMansion, airport, carwash, taxiDepot, courierDepot;
    Place hospital, raceCalle, raceBeach, raceHighway, raceGrove, raceKeys, rangeFlats, overseasStart, keySolano, sawgrassRoad;
    Place tenPalms, lakeTown, fortCastell, northCity, grove, redland, harlow, midtownPark, cafeBeach, flatsYard, kitStudio;
    Place downtownPenthouse, stadium;
    vec3 riverLaunch;      // boatyard slip on the Rio Sol
    vec3 riverMouth;       // where the river meets the bay
    vec3 bayCenter, bayNorth, baySouth, portWater, keyCoralWater, beachSea;
    vec3 sawgrassWater, sawgrassDeep, sawgrassDock;
    vec3 gulfWater;        // off Ten Palms (rescue scene)
    vec3 keySolanoMarina, keySolanoDock;
    vec3 solarisPlaza;     // Solaris One plaza center (82 m square)
    vec3 heliPad;          // airport helipad (a helicopter is parked there)
    vec3 pierRamp, pierEnd, pierPlatform;   // Sol Beach Pier: promenade ramp, far end, amusement platform
    vec3 portYard, portQuay, portTruck;     // Port Isle: yard lane, quay apron under the gantry cranes
    vec3 lighthouse;
};
Places gPlaces;

void computePlaces(GameWorld& g) {
    Places& P = gPlaces;
    if (P.ready) return;
    double t0 = TimeSeconds();
    P.mariApt = resolveBuilding(g, vec2(1720, 360), World::IK_APARTMENT);
    P.boatyard = resolvePlace(g, vec2(1650, 235));
    P.diner = resolveBuilding(g, vec2(1330, 330), World::IK_DINER);
    P.vargasGarage = resolveFrontage(g, vec2(2200, -480));
    P.stashHouse = resolveFrontage(g, vec2(1150, -620));
    P.solarisPier = resolvePlace(g, vec2(2560, 330));
    P.rookShop = resolveBuilding(g, vec2(1560, 2260), World::IK_CHOPSHOP);
    P.dexTrailer = resolveBuilding(g, vec2(1250, 2650), World::IK_TRAILER);
    P.pulseFm = resolveFrontage(g, vec2(3350, 640));
    P.policeHq = resolveBuilding(g, vec2(3050, -350), World::IK_POLICE);
    P.solarisOne = resolvePlace(g, vec2(3350, -810));
    P.sandovalOffice = resolveFrontage(g, vec2(3700, -1000));
    P.palmMotors = resolveBuilding(g, vec2(2700, 1300), World::IK_DEALERSHIP, "Palm Motors");
    P.gunFlats = resolveBuilding(g, vec2(800, 2900), World::IK_GUNSHOP, "Palmetto Arms");
    P.gunNorth = resolveBuilding(g, vec2(3300, 3900), World::IK_GUNSHOP, "Northside Arms");
    P.resprayCL = resolveBuilding(g, vec2(2300, -150), World::IK_MODSHOP, "Tide Customs Calle Luna");
    P.resprayBeach = resolveBuilding(g, vec2(5100, 2000), World::IK_MODSHOP, "Tide Customs Sol Beach");
    P.threads = resolveBuilding(g, vec2(5150, -300), World::IK_CLOTHES, "Threads");
    P.clubRiptide = resolvePlace(g, vec2(5380, 900));
    P.beachCondo = resolveFrontage(g, vec2(5120, 1500));
    P.beachPier = resolvePlace(g, vec2(5380, 1250));
    P.portGate = resolvePlace(g, vec2(4060, -200));
    P.keyCoral = resolveFrontage(g, vec2(4450, -4200));
    P.keyCoralMarina = resolvePlace(g, vec2(4200, -3900));
    P.sandovalMansion = resolvePlace(g, vec2(4300, 1300), 0.f, true);
    P.airport = resolvePlace(g, vec2(760, 1200));
    P.carwash = resolveBuilding(g, vec2(1800, 1500), World::IK_CARWASH, "Sunwash Car Wash");
    P.taxiDepot = resolveFrontage(g, vec2(2600, 700));
    P.courierDepot = resolveFrontage(g, vec2(1700, -300));
    P.hospital = resolveBuilding(g, vec2(1650, 1050), World::IK_HOSPITAL);
    P.raceCalle = resolvePlace(g, vec2(1100, -850));
    P.raceBeach = resolvePlace(g, vec2(5150, -2300));
    P.raceHighway = resolvePlace(g, vec2(300, -6000));
    P.raceGrove = resolvePlace(g, vec2(1500, -2500));
    P.raceKeys = resolvePlace(g, vec2(-7700, -9150));
    P.rangeFlats = resolvePlace(g, vec2(700, 3050));
    P.overseasStart = resolvePlace(g, vec2(200, -6100));
    P.keySolano = resolvePlace(g, vec2(-7900, -9200));
    P.sawgrassRoad = resolvePlace(g, vec2(-5060, 170), 0.f, true);
    P.tenPalms = resolvePlace(g, vec2(-8600, -2300));
    P.lakeTown = resolvePlace(g, vec2(250, 6750));
    P.fortCastell = resolvePlace(g, vec2(4200, 7600));
    P.northCity = resolvePlace(g, vec2(2600, 4300));
    P.grove = resolvePlace(g, vec2(2000, -3500));
    P.redland = resolveFrontage(g, vec2(800, -5400));
    P.harlow = resolvePlace(g, vec2(-5000, 4850));
    P.midtownPark = resolvePlace(g, vec2(3100, 1600));
    P.cafeBeach = resolveFrontage(g, vec2(5200, 300));
    P.flatsYard = resolvePlace(g, vec2(400, 3700));
    P.kitStudio = resolveFrontage(g, vec2(600, 3400));
    P.downtownPenthouse = resolveBuilding(g, vec2(3528, -706), World::IK_CONDO, "Downtown Penthouse");   // top floor of a downtown condo tower
    P.stadium = resolvePlace(g, vec2(3565, 620));
    findWater(g, vec2(1650, 150), 1.5f, P.riverLaunch, 200.f);
    findWater(g, vec2(3950, 160), 2.f, P.riverMouth, 400.f);
    findWater(g, vec2(4400, 600), 2.5f, P.bayCenter);
    findWater(g, vec2(4500, 2500), 2.5f, P.bayNorth);
    findWater(g, vec2(4500, -1500), 2.5f, P.baySouth);
    findWater(g, vec2(4700, -300), 2.5f, P.portWater);
    findWater(g, vec2(4000, -3300), 2.5f, P.keyCoralWater);
    findWater(g, vec2(5700, 800), 3.f, P.beachSea);
    findWater(g, vec2(-5000, -130), 0.8f, P.sawgrassWater, 1500.f);
    findWater(g, vec2(-5600, 300), 0.8f, P.sawgrassDeep, 1800.f);
    findWater(g, vec2(-9200, -2600), 3.f, P.gulfWater);
    findWater(g, vec2(-7600, -9500), 2.f, P.keySolanoMarina);
    // landmark sites (fixed layout, see world/sites.cpp)
    P.solarisPlaza = vec3(3350.f, -750.f, groundAt(g, 3350.f, -750.f));
    P.heliPad = vec3(652.f, 1112.f, groundAt(g, 652.f, 1112.f));
    {
        float shoreX = 5480.f;
        for (float x = 5000.f; x < 5900.f; x += 2.f)
            if (g.map->isWater(x, 1250.f)) {
                shoreX = x;
                break;
            }
        P.pierRamp = vec3(shoreX - 74.f, 1250.f, groundAt(g, shoreX - 74.f, 1250.f));
        P.pierPlatform = vec3(shoreX + 100.f, 1250.f, 5.6f);
        P.pierEnd = vec3(shoreX + 385.f, 1250.f, 5.6f);
        float z = g.groundHeight(P.pierEnd.x, P.pierEnd.y, 12.f);
        if (z > 2.f && z < 9.f) P.pierEnd.z = z;
        z = g.groundHeight(P.pierPlatform.x, P.pierPlatform.y, 12.f);
        if (z > 2.f && z < 9.f) P.pierPlatform.z = z;
    }
    P.portYard = vec3(4290.f, -415.f, groundAt(g, 4290.f, -415.f, 10.f));
    P.portQuay = vec3(4535.f, -380.f, groundAt(g, 4535.f, -380.f, 10.f));
    P.portTruck = vec3(4535.f, -340.f, groundAt(g, 4535.f, -340.f, 10.f));
    {
        // Sawgrass airboat dock: shore south of the Old Trail road near the boardwalk
        vec2 c(-5000.f, -40.f);
        vec3 w;
        findWater(g, c, 0.8f, w, 300.f);
        vec2 dir = length(w.xy() - c) > 1.f ? normalize(w.xy() - c) : vec2(0, -1);
        vec2 land = w.xy() - dir * 6.f;
        for (int k = 0; k < 40 && g.map->isWater(land.x, land.y); k++) land -= dir * 2.f;
        P.sawgrassDock = vec3(land, groundAt(g, land.x, land.y));
    }
    {
        vec2 A(-400, -6750), B(-4000, -9450), C(-8400, -9200);
        float t = 0.25f, u = 1.f - t;
        vec2 kc = A * (u * u) + B * (2.f * u * t) + C * (t * t);
        P.lighthouse = vec3(kc, groundAt(g, kc.x, kc.y));
    }
    {
        vec3 w = P.keySolanoMarina;
        vec2 dir = normalize(P.keySolano.pos.xy() - w.xy());
        vec2 land = w.xy();
        for (int k = 0; k < 200 && g.map->isWater(land.x, land.y); k++) land += dir * 3.f;
        P.keySolanoDock = vec3(land, groundAt(g, land.x, land.y));
    }
    P.ready = true;
    LOG("Story places resolved in %.1f ms (diner %.0f %.0f, Mari's %.0f %.0f, Rook's %.0f %.0f, Pulse FM %.0f %.0f, boatyard %.0f %.0f, river %.0f %.0f, "
        "sawgrass dock %.0f %.0f)",
        (TimeSeconds() - t0) * 1000.0, P.diner.door.x, P.diner.door.y, P.mariApt.door.x, P.mariApt.door.y, P.rookShop.door.x, P.rookShop.door.y,
        P.pulseFm.door.x, P.pulseFm.door.y, P.boatyard.pos.x, P.boatyard.pos.y, P.riverLaunch.x, P.riverLaunch.y, P.sawgrassDock.x, P.sawgrassDock.y);
}

// Offset a place along its street / outward (meters), snapped to the ground.
vec3 placeOffset(GameWorld& g, const Place& p, float along, float out) {
    vec2 q = p.pos.xy() + p.streetDir * along + p.outward * out;
    return vec3(q, groundAt(g, q.x, q.y, p.pos.z + 3.f));
}

// The nearest spot to `p` a person can stand on in the open: outside building footprints and site colliders by
// `margin`, dry, and with nothing overhead (a lot's outbuildings and sheds come and go as the world generator
// changes). Rings out to `maxR` meters; `p` itself when nothing nearer is free.
vec3 openGround(GameWorld& g, vec3 p, float margin = 0.8f, float maxR = 14.f) {
    auto free = [&](vec2 q, float& z) {
        if (g.buildings && g.buildings->pointInBuilding(q, margin)) return false;
        if (g.map && g.map->isWater(q.x, q.y)) return false;
        z = groundAt(g, q.x, q.y, p.z + 1.5f);
        if (groundAt(g, q.x, q.y, z + 40.f) > z + 0.6f) return false;   // a roof or a deck above the spot
        return !World::siteColliderNear(vec3(q, z), margin, 2.f);
    };
    float z = p.z;
    if (free(p.xy(), z)) return vec3(p.xy(), z);
    for (float r = 1.f; r <= maxR; r += 1.f) {
        int n = Max(8, (int)(r * kTwoPi / 1.2f));
        for (int i = 0; i < n; i++) {
            float a = kTwoPi * i / n;
            vec2 q = p.xy() + vec2(cosf(a), sinf(a)) * r;
            if (free(q, z)) return vec3(q, z);
        }
    }
    return p;
}

// Walk `along` meters up (+) or down (-) the street from a place, following the road network through intersections
// (straightest continuation, no highways). Returns the edge, distance, walking direction and the place's side of the
// street relative to the walking direction (+1 = left).
void roadWalk(GameWorld& g, const Place& p, float along, int& e, float& s, int& dir, float& sideW) {
    const World::RoadNetwork& R = *g.roads;
    e = p.edge;
    s = p.s;
    dir = along >= 0.f ? 1 : -1;
    sideW = p.side * (float)dir;
    float remaining = fabsf(along);
    for (int iter = 0; iter < 24; iter++) {
        const World::RoadEdge& ed = R.edges[e];
        float avail = dir > 0 ? ed.length - s : s;
        if (remaining <= avail) {
            s += (float)dir * remaining;
            break;
        }
        remaining -= avail;
        int node = dir > 0 ? ed.n1 : ed.n0;
        vec3 tEnd = ed.tangentAt(dir > 0 ? ed.length : 0.f) * (float)dir;
        int best = -1, bestDir = 1;
        float bestDot = -0.3f;
        for (int ne : R.nodes[node].edges) {
            if (ne == e) continue;
            const World::RoadEdge& c = R.edges[ne];
            if (c.cls == World::RC_HIGHWAY || c.cls == World::RC_RAMP || (c.flags & World::RF_ELEVATED)) continue;
            int cd = c.n0 == node ? 1 : -1;
            vec3 t0 = c.tangentAt(cd > 0 ? 0.f : c.length) * (float)cd;
            float dd = dot(normalize(t0.xy()), normalize(tEnd.xy()));
            if (dd > bestDot) {
                bestDot = dd;
                best = ne;
                bestDir = cd;
            }
        }
        if (best < 0) {
            s = dir > 0 ? ed.length : 0.f;
            break;
        }
        e = best;
        dir = bestDir;
        s = dir > 0 ? 0.f : R.edges[e].length;
    }
    const World::RoadEdge& ed = R.edges[e];
    float lo = Min(ed.cut0 + 2.f, ed.length * 0.5f), hi = Max(ed.length - ed.cut1 - 2.f, ed.length * 0.5f);
    s = Clamp(s, lo, hi);
}

// Curb spot `along` meters from a place (road-following), on the same side of the street. yawOut: right-hand
// parking heading at that spot.
vec3 curbOffset(GameWorld& g, const Place& p, float along, float* yawOut = nullptr) {
    if (p.edge < 0 || fabsf(along) < 0.01f) {
        if (yawOut) *yawOut = p.curbYaw;
        vec2 q = p.curb.xy() + p.streetDir * along;
        return vec3(q, groundAt(g, q.x, q.y, p.curb.z + 3.f));
    }
    int e, dir;
    float s, sideW;
    roadWalk(g, p, along, e, s, dir, sideW);
    const World::RoadEdge& ed = g.roads->edges[e];
    vec3 c = ed.posAt(s);
    vec2 w = normalize(ed.tangentAt(s).xy()) * (float)dir;
    vec2 out = perp(w) * sideW;
    const World::RoadClassInfo& info = World::roadInfo(ed.cls);
    vec2 q = c.xy() + out * (ed.halfWidth - Max(info.shoulder, 1.2f) * 0.5f - 0.4f);
    if (yawOut) {
        vec2 travel = perp(out);
        *yawOut = atan2f(-travel.x, travel.y);
    }
    return vec3(q, groundAt(g, q.x, q.y, c.z + 3.f));
}

// Spawn spot for a vehicle that will drive back toward the place: `dist` meters away along the road (sign picks the
// direction), in the lane that leads to the place, heading toward it.
vec3 approachSpot(GameWorld& g, const Place& p, float dist, float* yawOut) {
    if (p.edge < 0) {
        if (yawOut) *yawOut = p.curbYaw;
        return p.curb;
    }
    int e, dir;
    float s, sideW;
    roadWalk(g, p, dist, e, s, dir, sideW);
    const World::RoadEdge& ed = g.roads->edges[e];
    vec3 c = ed.posAt(s);
    vec2 back = -normalize(ed.tangentAt(s).xy()) * (float)dir;   // travel direction toward the place
    const World::RoadClassInfo& info = World::roadInfo(ed.cls);
    float lane = (ed.flags & World::RF_ONEWAY) ? 0.f : info.median * 0.5f + info.laneWidth * 0.5f;
    vec2 q = c.xy() + vec2(back.y, -back.x) * lane;
    if (yawOut) *yawOut = atan2f(-back.x, back.y);
    return vec3(q, groundAt(g, q.x, q.y, c.z + 3.f));
}

float yawTo(vec2 from, vec2 to) {
    vec2 d = to - from;
    return atan2f(-d.x, d.y);
}

vec2 dirFromYaw(float yaw) { return vec2(-sinf(yaw), cosf(yaw)); }

// ------------------------------------------------------------------------------------------------------------------
// Vehicles
// First available model of the listed classes (in order of preference); -1 if none exists in this build.
int pickModel(GameWorld& g, std::initializer_list<Vehicles::VehicleClass> classes, u32 seed = 0) {
    for (Vehicles::VehicleClass c : classes) {
        int m = g.findVehicleModel(c, seed);
        if (m >= 0) return m;
    }
    return -1;
}

bool hasClass(GameWorld& g, Vehicles::VehicleClass c) { return g.findVehicleModel(c, 0) >= 0; }

u32 classBit(Vehicles::VehicleClass c) { return 1u << (u32)c; }

int modelByName(GameWorld& g, const char* name) {
    for (int i = 0; i < (int)g.vassets.size(); i++)
        if (g.vassets[i].spec.name == name) return i;
    return -1;
}

// Mission vehicle at a position (z snapped by the vehicle sim), optional paint.
int spawnCar(GameWorld& g, int model, vec3 pos, float yaw, vec3 color = vec3(-1.f)) {
    if (model < 0) return -1;
    int v = g.mVehicle(model, dvec3(pos + vec3(0, 0, 0.35f)), yaw);
    if (v >= 0 && color.x >= 0.f) g.vehicles[v].color0 = color;
    return v;
}

// Vehicle with a driver and optional armed passengers (all mission peds).
int spawnCrewCar(GameWorld& g, int model, vec3 pos, float yaw, int driverChar, int passengers, int passengerChar, Faction f,
                 WeaponType w, float accuracy, std::vector<int>* crewOut = nullptr) {
    int v = spawnCar(g, model, pos, yaw);
    if (v < 0) return -1;
    int seats = Min((int)g.vassets[g.vehicles[v].model].spec.seats.size(), 4);
    for (int s = 0; s <= passengers && s < seats; s++) {
        int ci = s == 0 ? driverChar : passengerChar;
        int p = g.mPed(ci, dvec3(pos), yaw, f);
        if (p < 0) continue;
        g.warpPedIntoVehicle(p, v, s);
        if (w != WPN_FISTS) {
            g.giveWeapon(p, w, weaponInfo(w).clipSize * 6);
            g.peds[p].weapon = w;
        }
        g.peds[p].brain.accuracy = accuracy;
        g.peds[p].brain.type = BRAIN_NONE;
        if (crewOut) crewOut->push_back(p);
    }
    return v;
}

bool vehicleAlive(GameWorld& g, int v) { return v >= 0 && v < (int)g.vehicles.size() && g.vehicles[v].used && !g.vehicles[v].exploded && !g.vehicles[v].sim.wrecked; }

bool vehicleDisabled(GameWorld& g, int v) {
    if (!vehicleAlive(g, v)) return true;
    const Vehicle& veh = g.vehicles[v];
    return veh.sim.engineHealth <= 0.f || veh.sim.health <= 60.f || veh.sim.upsideDownTime > 3.f || veh.sim.engineFlooded;
}

float vehicleSpeed(GameWorld& g, int v) { return vehicleAlive(g, v) ? g.vehicles[v].sim.speed() : 0.f; }

vec3 vehPos(GameWorld& g, int v) { return v >= 0 && g.vehicles[v].used ? g.vehicles[v].sim.body.pos.toVec3() : vec3(0); }

// Remove the tracked mission blip of one vehicle (others stay).
void unblipVehicle(int veh) {
    if (veh < 0) return;
    auto& list = mission_detail::gTracked;
    list.erase(std::remove_if(list.begin(), list.end(), [veh](const TrackedBlip& b) { return b.vehicle == veh; }), list.end());
}

void teleportVehicle(GameWorld& g, int v, vec3 pos, float yaw) {
    if (v < 0 || !g.vehicles[v].used) return;
    Vehicles::resetVehicle(g.vehicles[v].sim, dvec3(pos + vec3(0, 0, 0.3f)), yaw);
    // the reset settles vehicles on the ground below; aircraft placed in the air and boats placed on the water keep
    // their height (the ground under a boat is the riverbed)
    Vehicles::VehicleState& s = g.vehicles[v].sim;
    if ((g.isAircraft(v) || g.isBoat(v)) && (float)s.body.pos.z < pos.z) s.body.pos.z = pos.z;
    s.sleeping = false;
}

// ------------------------------------------------------------------------------------------------------------------
// Peds
bool pedAlive(GameWorld& g, int p) { return p >= 0 && p < (int)g.peds.size() && g.peds[p].used && g.peds[p].health > 0.f; }

vec3 pedPos(GameWorld& g, int p) { return p >= 0 && g.peds[p].used ? g.peds[p].pos.toVec3() : vec3(0); }

vec3 playerPos(GameWorld& g) {
    Ped* pl = g.playerPed();
    return pl ? pl->pos.toVec3() : vec3(0);
}

void arm(GameWorld& g, int p, WeaponType w, int clips = 6) {
    if (p < 0) return;
    g.giveWeapon(p, w, Max(weaponInfo(w).clipSize, 1) * clips);
    g.peds[p].weapon = w;
}

void setCombat(GameWorld& g, int p, int target, float accuracy = -1.f) {
    if (!pedAlive(g, p)) return;
    Brain& b = g.peds[p].brain;
    b.type = BRAIN_COMBAT;
    b.target = target;
    b.timer = 0.f;
    b.thinkTimer = 0.f;
    if (accuracy >= 0.f) b.accuracy = accuracy;
    g.peds[p].animIn.stance = 0;
}

void setFollow(GameWorld& g, int p, int leader) {
    if (!pedAlive(g, p)) return;
    g.peds[p].brain.type = BRAIN_FOLLOW;
    g.peds[p].brain.target = leader;
    g.peds[p].animIn.stance = 0;
}

void setGoto(GameWorld& g, int p, vec3 goal, float speed) {
    if (!pedAlive(g, p)) return;
    g.peds[p].brain.type = BRAIN_GOTO;
    g.peds[p].brain.goal = dvec3(goal);
    g.peds[p].brain.speed = speed;
    g.peds[p].animIn.stance = 0;
}

void setIdle(GameWorld& g, int p, int stance = 0) {
    if (!pedAlive(g, p)) return;
    g.peds[p].brain.type = BRAIN_NONE;
    g.peds[p].animIn.stance = stance;
    g.peds[p].vel = vec3(0);
}

void setFlee(GameWorld& g, int p, int from) {
    if (!pedAlive(g, p)) return;
    g.peds[p].brain.type = BRAIN_FLEE;
    g.peds[p].brain.target = from;
    g.peds[p].brain.timer = 0.f;
}

void facePed(GameWorld& g, int p, vec3 at) {
    if (p < 0 || !g.peds[p].used) return;
    g.peds[p].yaw = yawTo(g.peds[p].pos.toVec3().xy(), at.xy());
}

void placePed(GameWorld& g, int p, vec3 pos, float yaw) {
    if (p < 0 || !g.peds[p].used) return;
    if (g.peds[p].vehicle >= 0) g.removePedFromVehicle(p, false);
    g.peds[p].pos = dvec3(pos.x, pos.y, groundAt(g, pos.x, pos.y, pos.z + 2.f));
    g.peds[p].yaw = yaw;
    g.peds[p].vel = vec3(0);
}

void gesture(GameWorld& g, int p, Anim::Clip c) {
    if (p >= 0 && g.peds[p].used && g.peds[p].state == PS_ONFOOT) g.peds[p].pendingAction = c;
}

// Living enemies (FAC_ENEMY mission peds) from a list.
int countAlive(GameWorld& g, const std::vector<int>& ids) {
    int n = 0;
    for (int id : ids) n += pedAlive(g, id) ? 1 : 0;
    return n;
}

int nearestAlive(GameWorld& g, const std::vector<int>& ids, vec3 from, float* distOut = nullptr) {
    int best = -1;
    float bd = 1e30f;
    for (int id : ids) {
        if (!pedAlive(g, id)) continue;
        float d = length(pedPos(g, id) - from);
        if (d < bd) {
            bd = d;
            best = id;
        }
    }
    if (distOut) *distOut = bd;
    return best;
}

// Player placement for mission (re)starts: on foot at pos, or driving a new mission vehicle of `model`.
int placePlayer(GameWorld& g, vec3 pos, float yaw, int model = -1, vec3 color = vec3(-1.f)) {
    Ped* pl = g.playerPed();
    if (!pl) return -1;
    if (pl->vehicle >= 0) g.removePedFromVehicle(g.player, false);
    freeRagdoll(pl->ragdoll);
    pl->state = PS_ONFOOT;
    pl->pos = dvec3(pos.x, pos.y, groundAt(g, pos.x, pos.y, pos.z + 3.f));
    pl->yaw = yaw;
    pl->vel = vec3(0);
    pl->grounded = true;
    g.rig.yaw = yaw;
    g.rig.cut = true;
    if (model < 0) return -1;
    int v = spawnCar(g, model, pos, yaw, color);
    if (v >= 0) g.warpPedIntoVehicle(g.player, v, 0);
    return v;
}

// ------------------------------------------------------------------------------------------------------------------
// Dialogue
DialogueLine line(const char* speaker, const std::string& text, int ped, u32 color) {
    DialogueLine l;
    l.speaker = speaker;
    l.text = text;
    l.ped = ped;
    l.color = color;
    return l;
}

// A cast member speaks (positional if the ped exists, else as a voice-over with the cast voice).
void say(GameWorld& g, int cast, int ped, const std::string& text, float pause = 0.25f) {
    DialogueLine l = line(kCast[cast].name, text, pedAlive(g, ped) ? ped : -1, kCast[cast].color);
    l.hasVoice = true;
    l.voice = castVoice(cast);
    l.spoken = castTags(cast) + speakableText(text);
    l.pause = pause;
    g.mSay(l);
}

// The protagonist `who` (0 Mari, 1 Dex) speaks; uses the player ped when that protagonist is the player.
void sayP(GameWorld& g, int who, int ped, const std::string& text, float pause = 0.25f) {
    int speaker = ped;
    if (speaker < 0 && g.protagonistIndex == who) speaker = g.player;
    DialogueLine l = line(who == 0 ? "Mari" : "Dex", text, pedAlive(g, speaker) ? speaker : -1, who == 0 ? kColMari : kColDex);
    l.hasVoice = true;
    l.voice = protagonistVoice(who);
    l.spoken = protagonistTags(who) + speakableText(text);
    l.pause = pause;
    g.mSay(l);
}

void sayMe(GameWorld& g, const std::string& text, float pause = 0.25f) { sayP(g, g.protagonistIndex, g.player, text, pause); }

// A cast member over a transmission channel: "[phone]" (calls), "[radio]" (heist walkie-talkies), "[megaphone]", "[pa]".
void channelLine(GameWorld& g, int cast, const std::string& text, const char* channel, float pause = 0.3f) {
    DialogueLine l = line(kCast[cast].name, text, -1, kCast[cast].color);
    l.phone = true;   // not positional
    l.hasVoice = true;
    l.voice = castVoice(cast);
    l.spoken = castTags(cast) + channel + speakableText(text);
    l.pause = pause;
    g.mSay(l);
}

void phoneLine(GameWorld& g, int cast, const std::string& text, float pause = 0.3f) { channelLine(g, cast, text, "[phone]", pause); }
void radioLine(GameWorld& g, int cast, const std::string& text, float pause = 0.3f) { channelLine(g, cast, text, "[radio]", pause); }

// A voice without a body (radio, TV, PA): `personaKey` is a speech persona ("newsreader_female", "dj", "cast_kit"...),
// `delivery` an optional style prefix such as "[news]" or "[dj]".
void narrator(GameWorld& g, const char* name, const std::string& text, const char* personaKey, const char* delivery = "", u32 color = kColOther) {
    DialogueLine l = line(name, text, -1, color);
    Speech::Persona p = Speech::persona(personaKey);
    l.hasVoice = true;
    l.voice = p.voice;
    l.spoken = p.tags() + delivery + speakableText(text);
    l.phone = true;   // not positional
    g.mSay(l);
}

// Phonetic spellings for names the letter-to-sound rules would mangle (subtitles keep the real spelling).
std::string speakableText(const std::string& text) {
    static const char* const kMap[][2] = {
        {"Calle Luna", "Kah-yeh Loona"}, {"Cuervos", "Kwair-vose"}, {"Cuervo", "Kwair-vo"}, {"Sandoval", "Sando-vahl"},
        {"Tomas", "Toh-mahss"}, {"Marisol", "Mahree-sole"}, {"Mari", "Mahri"}, {"Lucha", "Loo-chah"}, {"Ortega", "Or-tay-guh"},
        {"Navarro", "Nuh-varr-oh"}, {"Solaris", "So-lair-iss"}, {"Solano", "So-lah-no"}, {"Velez", "Veh-less"},
        {"Paredes", "Pah-ray-des"}, {"Ramiro", "Rah-mee-roh"}, {"Chuy", "Choo-ee"}, {"pozole", "po-so-leh"},
        {"mija", "mee-hah"}, {"mijo", "mee-hoe"}, {"Abuela", "Ah-bway-lah"}, {"gracias", "grah-see-us"},
        {"Oye", "Oy-yeh"}, {"Palmetto", "Pal-metto"}, {"Okahatchee", "Oka-hatchee"}, {"Isla Estrella", "Eesla Es-tray-ah"},
        {"Villanueva", "Vee-ya-nway-va"}, {"Ruiz", "Roo-eez"}, {"Ernesto", "Er-nes-toe"}, {"Batista", "Ba-teesta"}, {"Dagostino", "Dago-steeno"},
    };
    std::string s = text;
    for (const auto& m : kMap) {
        const std::string from = m[0], to = m[1];
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            bool startOk = pos == 0 || !isalpha((unsigned char)s[pos - 1]);
            size_t end = pos + from.size();
            bool endOk = end >= s.size() || !isalpha((unsigned char)s[end]);
            if (startOk && endOk) {
                s.replace(pos, from.size(), to);
                pos += to.size();
            } else {
                pos = end;
            }
        }
    }
    return s;
}

// ------------------------------------------------------------------------------------------------------------------
// Cutscene shots
CutsceneShot shot(vec3 from, vec3 at, float duration, float fov = 50.f) {
    CutsceneShot s;
    s.pos = s.pos2 = dvec3(from);
    s.target = s.target2 = dvec3(at);
    s.duration = duration;
    s.fov = fov;
    return s;
}

CutsceneShot shotMove(vec3 from, vec3 at, vec3 from2, vec3 at2, float duration, float fov = 50.f) {
    CutsceneShot s;
    s.pos = dvec3(from);
    s.target = dvec3(at);
    s.pos2 = dvec3(from2);
    s.target2 = dvec3(at2);
    s.duration = duration;
    s.fov = fov;
    return s;
}

// Slow arc around a subject (chord approximation between two angles).
CutsceneShot shotArc(vec3 center, float radius, float height, float a0, float a1, float duration, float fov = 45.f) {
    vec3 p0 = center + vec3(cosf(a0) * radius, sinf(a0) * radius, height);
    vec3 p1 = center + vec3(cosf(a1) * radius, sinf(a1) * radius, height);
    return shotMove(p0, center + vec3(0, 0, 1.2f), p1, center + vec3(0, 0, 1.3f), duration, fov);
}

// Over-the-shoulder: camera behind `viewer` looking at `subject` (both feet positions).
CutsceneShot shotOver(vec3 viewer, vec3 subject, float duration, float side = 1.f, float fov = 40.f) {
    vec2 d = normalize(subject.xy() - viewer.xy());
    vec2 r(d.y, -d.x);
    vec3 cam = viewer + vec3(-d * 1.4f + r * (0.55f * side), 1.72f);
    vec3 at = subject + vec3(0, 0, 1.55f);
    vec3 cam2 = cam + vec3(d * 0.25f, 0.f);
    return shotMove(cam, at, cam2, at, duration, fov);
}

// Two people in frame from the side of the line between them.
CutsceneShot shotTwo(vec3 a, vec3 b, float duration, float dist = 4.2f, float fov = 42.f, float side = 1.f) {
    vec3 mid = (a + b) * 0.5f;
    vec2 d = normalize(b.xy() - a.xy());
    vec2 n = perp(d) * side;
    float sep = length(b.xy() - a.xy());
    vec3 cam = mid + vec3(n * (dist + sep * 0.6f), 1.65f);
    vec3 cam2 = cam + vec3(d * 0.6f, 0.05f);
    return shotMove(cam, mid + vec3(0, 0, 1.45f), cam2, mid + vec3(0, 0, 1.5f), duration, fov);
}

// High establishing shot drifting toward a location.
CutsceneShot shotEstablish(vec3 at, float yaw, float dist, float height, float duration, float fov = 55.f) {
    vec2 back = -dirFromYaw(yaw);
    vec3 p0 = at + vec3(back * dist, height);
    vec3 p1 = at + vec3(back * (dist * 0.8f), height * 0.85f);
    return shotMove(p0, at + vec3(0, 0, 4.f), p1, at + vec3(0, 0, 3.f), duration, fov);
}

// True when nothing solid stands between a camera position and what it looks at. The ray runs from the subject out to
// the camera, so a camera buried inside a building across the street counts as blocked (boxes that contain the ray's
// start are ignored, which also skips the subject's own facade).
bool clearView(GameWorld& g, dvec3 cam, dvec3 subject) {
    vec3 d = rel(cam, subject);
    float len = length(d);
    if (len < 1.f) return true;
    WorldHit h;
    return !g.raycast(subject, d / len, len - 0.5f, h, -1, -1, false, false);
}

// Establishing shot with a clear view: the requested angle first, then swinging around the subject and climbing.
// Nothing is added when every angle is blocked (a scene inside an interior), so the cutscene opens on its first
// close shot instead. The shot plays a moment before the first line.
bool establish(GameWorld& g, std::vector<CutsceneShot>& shots, vec3 at, float yaw, float dist, float height, float duration,
               float fov = 55.f) {
    static const float kSwing[] = {0.f, 0.45f, -0.45f, 0.9f, -0.9f, 1.4f, -1.4f};
    for (float lift : {1.f, 1.6f, 2.4f})
        for (float sw : kSwing) {
            CutsceneShot s = shotEstablish(at, yaw + sw, dist * (lift > 2.f ? 0.8f : 1.f), height * lift, duration, fov);
            if (!clearView(g, s.pos, s.target) || !clearView(g, s.pos2, s.target2)) continue;
            s.leadIn = Min(1.4f, duration * 0.35f);
            shots.push_back(s);
            return true;
        }
    return false;
}

// Chase-cam style shot looking at a vehicle from behind/side.
CutsceneShot shotVehicle(GameWorld& g, int v, float duration, float side = 1.f, float fov = 50.f) {
    vec3 p = vehPos(g, v);
    vec3 f = v >= 0 ? g.vehicles[v].sim.forward() : vec3(0, 1, 0);
    vec2 fd = normalize(f.xy());
    vec2 r(fd.y, -fd.x);
    vec3 cam = p + vec3(-fd * 7.f + r * (3.f * side), 2.2f);
    vec3 cam2 = p + vec3(-fd * 5.f + r * (4.5f * side), 1.6f);
    return shotMove(cam, p + vec3(0, 0, 0.8f), cam2, p + vec3(fd * 2.f, 0.9f), duration, fov);
}

// A slow push-in from a point inside an interior toward two people talking (the room's wide opening shot)
CutsceneShot shotRoom(const InteriorStage& in, vec3 cameraLocal, vec3 a, vec3 b, float duration) {
    vec3 cam = in.at(cameraLocal), mid = (a + b) * 0.5f + vec3(0.f, 0.f, 1.35f);
    vec3 d = mid - cam;
    return shotMove(cam, mid, cam + d * 0.12f, mid, duration, 48.f);
}

// ------------------------------------------------------------------------------------------------------------------
// The SkyLine (the elevated metro loop run by game/transit_game.cpp) as missions see it: its trains are ordinary
// vehicles (three "SkyLine" cars each, moved along the viaduct by the transit system) and its stations are world data
// (world/transit.h). A mission ped rides by being seated in a car while it dwells at a platform and steps off by being
// taken out at a later one; the transit system only fills and empties the seats of its own riders.
bool metroReady() { return World::gTransit && World::gTransit->metro.stations.size() >= 2; }

bool isMetroCar(const GameWorld& g, int v) {
    if (v < 0 || v >= (int)g.vehicles.size() || !g.vehicles[v].used) return false;
    int m = g.vehicles[v].model;
    return m >= 0 && m < (int)g.vassets.size() && g.vassets[m].spec.name.compare(0, 7, "SkyLine") == 0;
}

int metroStationByName(const char* name) {
    if (!metroReady()) return -1;
    const World::MetroLine& L = World::gTransit->metro;
    for (int i = 0; i < (int)L.stations.size(); i++)
        if (L.stations[i].name == name) return i;
    return -1;
}

int metroStationNear(vec2 p) {
    if (!metroReady()) return -1;
    const World::MetroLine& L = World::gTransit->metro;
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < (int)L.stations.size(); i++) {
        float d = length(L.stations[i].pos - p);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

// Platform side: 0 the outer platform (counter-clockwise service), 1 the inner one (clockwise service)
float metroSideSign(int side) { return side == 0 ? 1.f : -1.f; }

// A car standing in a station (stopped inside the platform zone): the station, and the platform side of its track;
// -1 while it moves or stands between stations.
int metroCarStation(const GameWorld& g, int v, int* sideOut = nullptr) {
    if (!isMetroCar(g, v) || !metroReady()) return -1;
    const Vehicle& c = g.vehicles[v];
    if (length(c.sim.body.vel) > 0.3f) return -1;
    const World::MetroLine& L = World::gTransit->metro;
    vec2 p = c.sim.body.pos.toVec3().xy();
    for (int i = 0; i < (int)L.stations.size(); i++) {
        const World::MetroStation& S = L.stations[i];
        vec2 d = p - S.pos;
        float along = dot(d, S.dir), lat = dot(d, S.right());
        if (fabsf(along) > World::transit_dims::kPlatformHalfLen || fabsf(lat) > 4.f) continue;
        if (sideOut) *sideOut = lat > 0.f ? 0 : 1;
        return i;
    }
    return -1;
}

// The car of a train dwelling at a station's platform side that is nearest to `from` (-1 when no train stands there)
int metroCarAt(const GameWorld& g, int st, int side, vec2 from) {
    int best = -1;
    float bd = 1e9f;
    for (int v = 0; v < (int)g.vehicles.size(); v++) {
        int sd = -1;
        if (!isMetroCar(g, v) || metroCarStation(g, v, &sd) != st || sd != side) continue;
        float d = length(g.vehicles[v].sim.body.pos.toVec3().xy() - from);
        if (d < bd) {
            bd = d;
            best = v;
        }
    }
    return best;
}

// Where a door of a car standing at a platform opens onto it (door -1, 0, 1 along the car), `out` metres out from the
// car side, at platform height
vec3 metroDoor(const GameWorld& g, int v, int st, int side, int door, float out) {
    const World::MetroStation& S = World::gTransit->metro.stations[st];
    const Vehicle& c = g.vehicles[v];
    vec2 f = normalize(c.sim.forward().xy());
    vec2 p = c.sim.body.pos.toVec3().xy() + f * (5.6f * door) + S.right() * (metroSideSign(side) * (1.45f + out));
    return vec3(p, S.platformZ());
}

// A free passenger seat of a car (-1 when full)
int metroFreeSeat(const GameWorld& g, int v) {
    if (v < 0 || !g.vehicles[v].used) return -1;
    const Vehicle& c = g.vehicles[v];
    int n = Min((int)g.vassets[c.model].spec.seats.size(), 8);
    for (int s = 1; s < n; s++)
        if (c.seats[s] < 0) return s;
    return -1;
}

// On foot between the street and a platform: the stair beside the station, the fare gates, the platform (the station
// layout of world/transit.h, the same way the transit riders walk). Up ends at `platformAlong` on the platform, down
// ends on the street at the stair foot.
std::vector<vec3> metroStairPath(int st, int side, bool up, float platformAlong) {
    const World::MetroStation& S = World::gTransit->metro.stations[st];
    const float L = metroSideSign(side), H = World::transit_dims::kPlatformHalfLen;
    const int E = S.exitEnd[side];
    const float pz = S.platformZ(), sz = S.streetZ, gate = E * (H - 3.3f + 0.675f);
    std::vector<vec3> p = {S.local(E * (H - 3.4f - 36.f), L * 10.8f, sz), S.local(E * (H - 3.4f - 31.5f), L * 9.4f, sz),
                           S.local(E * (H - 3.0f), L * 9.4f, pz),         S.local(gate, L * 10.f, pz),
                           S.local(gate, L * 6.f, pz),                    S.local(Clamp(platformAlong, -H + 4.f, H - 4.f), L * 4.8f, pz)};
    if (!up) std::reverse(p.begin(), p.end());
    return p;
}

// A ped walking a list of points (BRAIN_GOTO leg by leg). A ped that makes no headway for a while is set down at the
// point it was heading for, so a scripted walk always arrives.
struct PedPath {
    int ped = -1;
    std::vector<vec3> pts;
    int wp = 0;
    float speed = 1.5f, stuck = 0.f, lastDist = 1e9f;
    void start(GameWorld& g, int p, const std::vector<vec3>& path, float spd) {
        ped = p;
        pts = path;
        wp = 0;
        speed = spd;
        stuck = 0.f;
        lastDist = 1e9f;
        if (!pts.empty()) setGoto(g, p, pts[0], spd);
    }
    bool done() const { return wp >= (int)pts.size(); }
    // true once the last point is reached
    bool update(GameWorld& g, float dt) {
        if (done() || !pedAlive(g, ped) || g.peds[ped].state != PS_ONFOOT) return done();
        vec3 pp = pedPos(g, ped);
        float d = length(pts[wp].xy() - pp.xy());
        stuck = d < lastDist - 0.05f ? 0.f : stuck + dt;
        lastDist = Min(lastDist, d);
        if (stuck > 5.f) {
            placePed(g, ped, pts[wp], g.peds[ped].yaw);
            d = 0.f;
        }
        if (d < 0.9f) {
            wp++;
            stuck = 0.f;
            lastDist = 1e9f;
            if (!done()) setGoto(g, ped, pts[wp], speed);
        } else if (g.peds[ped].brain.type != BRAIN_GOTO) {
            setGoto(g, ped, pts[wp], speed);
        }
        return done();
    }
};

}  // namespace mu

// Runtime framing for dialogue that outlasts a cutscene's scripted shots: an over-the-shoulder shot of the speaker
// from the nearest listener (alternating sides), or the previous shot held for voice-overs and phone lines.
bool GameWorld::speakerShot(int speaker, const CutsceneShot* prev, float lineTime, CutsceneShot& out) {
    float dur = Clamp(lineTime + 0.35f, 1.2f, 9.f);
    bool onFoot = speaker >= 0 && speaker < (int)peds.size() && peds[speaker].used && peds[speaker].health > 0.f && peds[speaker].state == PS_ONFOOT;
    if (!onFoot) {
        if (!prev) return false;
        out = *prev;
        out.pos = out.pos2;
        out.target = out.target2;
        out.duration = dur;
        out.speaker = speaker;
        return true;
    }
    vec3 sp = peds[speaker].pos.toVec3();
    // listener: the player unless the player speaks, else the closest mission ped
    int listener = -1;
    float best = 12.f;
    auto consider = [&](int id) {
        if (id < 0 || id == speaker || id >= (int)peds.size() || !peds[id].used || peds[id].health <= 0.f || peds[id].state != PS_ONFOOT) return;
        float d = length(peds[id].pos.toVec3() - sp);
        if (d < best && d > 0.5f) {
            best = d;
            listener = id;
        }
    };
    consider(player);
    if (listener < 0 || speaker == player)
        for (int id : gMissions.peds) consider(id);
    float side = (gMissions.autoShots & 1) ? -1.f : 1.f;
    // the line's mood picks the camera: heated lines push in handheld, whispers get close, calm ones stay wide
    const std::string& txt = gMissions.lines.empty() ? std::string() : gMissions.lines.front().text;
    bool heated = txt.find("[angry") != std::string::npos || txt.find("[shout") != std::string::npos || txt.find("[scared") != std::string::npos;
    bool hushed = txt.find("[whisper") != std::string::npos || txt.find("[sad") != std::string::npos;
    if (listener >= 0) {
        out = mu::shotOver(peds[listener].pos.toVec3(), sp, dur, side, heated ? 34.f : (hushed ? 32.f : 38.f));
        if (heated || hushed) {
            // push in: end the move a step closer to the speaker
            vec3 d = normalize(sp - peds[listener].pos.toVec3());
            out.pos2 = out.pos2 + dvec3(d * (heated ? 0.7f : 0.45f));
        }
        out.handheld = heated ? 0.8f : 0.f;
    }
    else {
        // nobody to look over: a gentle medium shot from in front of the speaker
        vec2 f = mu::dirFromYaw(peds[speaker].yaw);
        vec3 cam = sp + vec3(f * 2.6f + vec2(f.y, -f.x) * 0.8f * side, 1.62f);
        out = mu::shotMove(cam, sp + vec3(0, 0, 1.55f), cam + vec3(f * -0.2f, 0.f), sp + vec3(0, 0, 1.58f), dur, 40.f);
    }
    out.speaker = speaker;
    mission_detail::fixShot(*this, out);
    return true;
}

namespace mu {

// ------------------------------------------------------------------------------------------------------------------
// Story props drawn as dynamic models: a stack of long wooden rifle crates with stencils (Sawgrass Run hammock).
Render::Model* gCrateModel = nullptr;

void drawCrates(GameWorld& g, vec3 pos, float yaw, int count, u32 seed) {
    if (!g.renderer) return;
    if (!gCrateModel) {
        MeshData m;
        u32 wood = makeMat(MAT_WOOD), paint = makeMat(MAT_METAL_PAINTED);
        u32 cw = packRGBA8(0.42f, 0.33f, 0.2f, 1.f), dark = packRGBA8(0.25f, 0.19f, 0.11f, 1.f), stencil = packRGBA8(0.92f, 0.9f, 0.84f, 1.f);
        // crate 1.2 x 0.5 x 0.42 m with end battens and a stencil band on the long side (-Y)
        m.boxAA(vec3(-0.6f, -0.25f, 0.f), vec3(0.6f, 0.25f, 0.42f), cw, wood, true);
        m.boxAA(vec3(-0.63f, -0.27f, 0.f), vec3(-0.53f, 0.27f, 0.44f), dark, wood);
        m.boxAA(vec3(0.53f, -0.27f, 0.f), vec3(0.63f, 0.27f, 0.44f), dark, wood);
        m.boxAA(vec3(-0.3f, -0.253f, 0.14f), vec3(0.3f, -0.251f, 0.28f), stencil, paint);
        gCrateModel = g.renderer->dynamic->createModel(m);
    }
    vec2 ax(cosf(yaw), sinf(yaw)), ay(-ax.y, ax.x);
    for (int i = 0; i < count; i++) {
        u32 h = hash32(seed + (u32)i * 7919u);
        int layer = i / 3, slot = i % 3;
        vec2 off = ay * ((slot - 1) * 0.56f) + ax * ((hashToFloat(h) - 0.5f) * 0.12f);
        vec3 p(pos.xy() + off, pos.z + layer * 0.44f);
        float y = yaw + (hashToFloat(h >> 8) - 0.5f) * 0.12f;
        Render::DrawItem di;
        di.model = gCrateModel;
        di.pos = dvec3(p);
        di.rot = mat3FromQuat(quatAxisAngle(vec3(0, 0, 1), y));
        di.scale = vec3(1.f);
        di.tint0 = vec4(1.f, 1.f, 1.f, 0.f);
        di.castShadow = true;
        di.id = 0xE000000000ull + (u64)seed * 64u + (u64)i;
        g.renderer->dynamic->submit(di);
    }
}

// ------------------------------------------------------------------------------------------------------------------
// Adaptive score: style 0 neon noir, 1 chase, 2 stealth, 3 heist (see audio/music.cpp ScoreGen::init)
int scoreMood(int style, int variant) {
    int found = 0;
    for (int k = 1; k < 4000; k++) {
        u32 seed = hash32((u32)k * 2654435761u + 0x5C0Eu);
        if ((int)(seed % 4u) != style) continue;
        if (found++ == variant) return k;
    }
    return style;
}

void score(int style, float intensity, int variant = 0) {
#ifdef HAVE_AUDIO
    Audio::setScore(scoreMood(style, variant), intensity);
#else
    (void)style;
    (void)intensity;
    (void)variant;
#endif
}

enum ScoreStyle : int { SC_NOIR = 0, SC_CHASE = 1, SC_STEALTH = 2, SC_HEIST = 3 };

// Weather a story mission asks for (a WeatherKind of weather.cpp, -1 none): the app applies it at its next weather step,
// cutting straight to it when `instant` (a mission start under black), else blending over half a minute.
int gWeatherRequest = -1;
bool gWeatherInstant = false;
void requestWeather(int kind, bool instant) {
    gWeatherRequest = kind;
    gWeatherInstant = instant;
}

// ------------------------------------------------------------------------------------------------------------------
// Story flags (extended state lives at kExtBase..)
int flag(GameWorld& g, int i) { return i >= 0 && i < (int)g.storyFlags.size() ? g.storyFlags[i] : 0; }
void setFlag(GameWorld& g, int i, int v) {
    if (i < 0) return;
    if ((int)g.storyFlags.size() <= i) g.storyFlags.resize(Max(i + 1, (int)kFlagCount), 0);
    g.storyFlags[i] = v;
}

void money(GameWorld& g, long long delta) {
    g.pinfo.money += delta;
    if (g.pinfo.money < 0) g.pinfo.money = 0;
}

// Open-world markers (shops, safehouses, businesses): rebuilt every frame, drawn by the open world update.
std::vector<Marker> gWorldMarkers;
void worldMarker(vec3 pos, float radius, vec3 color) {
    Marker m;
    m.pos = dvec3(pos);
    m.radius = radius;
    m.color = color;
    gWorldMarkers.push_back(m);
}

}  // namespace mu

std::string speakableText(const std::string& text) { return mu::speakableText(text); }

}  // namespace Game
