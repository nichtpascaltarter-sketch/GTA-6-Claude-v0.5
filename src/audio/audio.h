// Public audio API used by the game. Implementation: software mixer (48 kHz stereo float)
// running on its own thread, output through WASAPI shared mode. Every sound, music track and
// voice is synthesized procedurally at runtime.
#pragma once
#include "../core/base.h"
#include "../core/math.h"

namespace Audio {

// ---------------------------------------------------------------------------------------------
// One-shot sound effects (synthesized; variations are randomized per play).
enum Sfx : int {
    SFX_NONE = 0,
    // Footsteps by surface
    SFX_STEP_CONCRETE, SFX_STEP_GRASS, SFX_STEP_WOOD, SFX_STEP_METAL, SFX_STEP_SAND, SFX_STEP_WATER, SFX_STEP_GRAVEL,
    // Weapons
    SFX_PISTOL, SFX_SMG, SFX_RIFLE, SFX_SHOTGUN, SFX_SNIPER, SFX_ROCKET_LAUNCH, SFX_SILENCED,
    SFX_RELOAD, SFX_DRY_FIRE, SFX_WEAPON_SWITCH, SFX_SHELL_CASING,
    SFX_BULLET_WHIZ, SFX_IMPACT_CONCRETE, SFX_IMPACT_METAL, SFX_IMPACT_GLASS, SFX_IMPACT_FLESH, SFX_IMPACT_DIRT,
    SFX_IMPACT_WATER, SFX_IMPACT_WOOD,
    SFX_EXPLOSION, SFX_EXPLOSION_SMALL, SFX_GRENADE_BOUNCE,
    // Melee / bodies
    SFX_PUNCH, SFX_KICK, SFX_BODY_FALL, SFX_GRUNT_MALE, SFX_GRUNT_FEMALE, SFX_SCREAM_MALE, SFX_SCREAM_FEMALE,
    // Vehicles
    SFX_CAR_DOOR_OPEN, SFX_CAR_DOOR_CLOSE, SFX_CAR_CRASH_LIGHT, SFX_CAR_CRASH_HEAVY, SFX_GLASS_BREAK,
    SFX_ENGINE_START, SFX_ENGINE_STOP, SFX_GEAR_SHIFT, SFX_TIRE_POP, SFX_METAL_SCRAPE, SFX_BIKE_KICKSTAND,
    SFX_SPLASH_SMALL, SFX_SPLASH_BIG, SFX_BOAT_SLAM,
    // World
    SFX_THUNDER, SFX_DOG_BARK, SFX_SEAGULL, SFX_BIRD_CHIRP, SFX_CAR_ALARM_CHIRP, SFX_DOOR_BUZZ, SFX_CASH_REGISTER,
    SFX_BELL, SFX_HELI_FLYBY,
    // UI / game flow
    SFX_UI_MOVE, SFX_UI_SELECT, SFX_UI_BACK, SFX_UI_ERROR, SFX_UI_NOTIFY, SFX_UI_TEXT, SFX_PHONE_RING, SFX_PHONE_MSG,
    SFX_PICKUP_CASH, SFX_PICKUP_WEAPON, SFX_PICKUP_HEALTH, SFX_PICKUP_COLLECTIBLE, SFX_CHECKPOINT,
    SFX_MISSION_PASSED, SFX_MISSION_FAILED, SFX_WASTED, SFX_BUSTED, SFX_WANTED_UP, SFX_WANTED_LOST,
    SFX_CAMERA_SHUTTER, SFX_PURCHASE, SFX_RACE_COUNTDOWN, SFX_RACE_GO,
    SFX_WHOOSH,   // air swing of a fist / bat / blade
    // Wildlife (game/wildlife.cpp)
    SFX_WING_FLAP, SFX_PIGEON_COO, SFX_GATOR_HISS, SFX_GATOR_BELLOW, SFX_JAW_SNAP, SFX_DOG_GROWL, SFX_DOG_YELP, SFX_CAT_MEOW,
    SFX_COW_MOO, SFX_HORSE_NEIGH, SFX_PARROT_SQUAWK, SFX_HERON_CALL, SFX_DOLPHIN_CALL, SFX_ANIMAL_BLOW, SFX_RACCOON_CHITTER,
    SFX_DEER_SNORT,
    // Public transit (game/transit_game.cpp)
    SFX_TRANSIT_CHIME, SFX_TRAIN_DOORS, SFX_RAIL_CLACK, SFX_SHIP_HORN,
    SFX_COUNT
};

// Continuous, parameter-driven sound sources.
enum EmitterType : int {
    EMIT_ENGINE = 0,     // params: rpm (0..1 normalized), throttle (0..1), load (0..1), engineKind (see EngineKind)
    EMIT_SIREN,          // params: mode (0 wail, 1 yelp, 2 hi-lo, 3 ambulance), -, -, -
    EMIT_HORN,           // params: pitchVariant (0..1)
    EMIT_TIRE_SKID,      // params: slip intensity (0..1), surface (0 asphalt, 1 dirt)
    EMIT_WIND_RUSH,      // params: speed (m/s)
    EMIT_FIRE,           // params: size (0..1)
    EMIT_ROTOR,          // helicopter rotor: params rpm (0..1), throttle
    EMIT_PROP,           // propeller plane: params rpm (0..1), throttle
    EMIT_JET,            // jet engine: params rpm (0..1), throttle
    EMIT_BOAT,           // outboard motor: params rpm, throttle, in-water (0/1)
    EMIT_WATER_WAKE,     // hull water noise: params speed
    EMIT_ALARM,          // car alarm loop
    EMIT_RADIO_WORLD,    // positional music from a world source (club, parked car): params station index
    EMIT_CROWD,          // crowd murmur/panic: params density (0..1), panic (0..1)
    EMIT_COUNT
};

enum EngineKind : int {
    ENGINE_I4 = 0, ENGINE_V6, ENGINE_V8, ENGINE_V12, ENGINE_TRUCK_DIESEL, ENGINE_ELECTRIC, ENGINE_BIKE_SPORT,
    ENGINE_BIKE_CRUISER, ENGINE_SCOOTER, ENGINE_COUNT
};

struct Listener {
    vec3 pos, vel;
    vec3 forward = vec3(0, 1, 0), up = vec3(0, 0, 1);
    float interior = 0.f;  // 0 outdoors .. 1 enclosed (tunnel, garage, car cabin) -> reverb/low-pass
    float inVehicle = 0.f; // 0..1 muffles outside sounds when inside a closed vehicle
};

// Ambient bed mix, updated every frame from the world around the listener.
struct Ambience {
    float urban = 0.5f;      // traffic hum, distant city
    float nature = 0.f;      // birds by day, crickets/frogs by night
    float coast = 0.f;       // surf, seagulls
    float wetland = 0.f;     // insects, frogs, water
    float rain = 0.f;        // 0..1
    float wind = 0.2f;       // 0..1
    float timeOfDay = 12.f;  // hours 0..24
    float underwater = 0.f;  // 0/1
};

struct VoiceParams {
    float pitch = 120.f;       // base F0 in Hz (male ~100-130, female ~180-230)
    float formantScale = 1.f;  // vocal tract length scaling (female ~1.15, child ~1.3)
    float speed = 1.f;         // speaking rate multiplier
    float breathiness = 0.1f;  // 0..1
    float roughness = 0.f;     // 0..1 (creak/jitter)
    float expressiveness = 1.f;// intonation range multiplier
};

typedef u32 SoundHandle;    // 0 = invalid
typedef u32 EmitterHandle;  // 0 = invalid

bool init();       // starts WASAPI output + mixing thread; returns false if no audio device (game continues silently)
void shutdown();
void update(const Listener& listener, float dt);  // main thread, once per frame
void setAmbience(const Ambience& a);
// Crowd walla (unintelligible murmur of many voices) for populated places. density: 0 nobody .. ~0.5 busy street ..
// 1 packed beach / club (e.g. Clamp(pedsWithin25m / 30.f, 0, 1)); placeType: CrowdPlace; panic: 0 calm .. 1 the crowd
// is fleeing (gunfire, explosions): the murmur turns into shouts and screams (fast swell, slow ~3 s recovery). Call
// every frame (cheap); level and place changes crossfade; outdoor crowds are muffled by Listener::interior/inVehicle.
enum CrowdPlace : int { CROWD_STREET = 0, CROWD_BEACH, CROWD_CLUB, CROWD_MALL, CROWD_PLACE_COUNT };
void setCrowd(float density, int placeType, float panic = 0.f);
void setPaused(bool paused);      // pauses world sounds (UI, radio-in-menu continue)
void setSlowMotion(float factor); // 1 = normal; <1 pitches world sounds down

// Volumes 0..1
void setMasterVolume(float v);
void setSfxVolume(float v);
void setMusicVolume(float v);  // radio + score
void setVoiceVolume(float v);

// One-shots
SoundHandle play(Sfx id, vec3 pos, float volume = 1.f, float pitch = 1.f);
SoundHandle play2D(Sfx id, float volume = 1.f, float pitch = 1.f);
void stop(SoundHandle h);
bool isPlaying(SoundHandle h);

// Emitters
EmitterHandle createEmitter(EmitterType type);
void setEmitter(EmitterHandle h, vec3 pos, vec3 vel, float p0, float p1 = 0.f, float p2 = 0.f, float p3 = 0.f,
                float volume = 1.f);
// Upgrades heard from an EMIT_ENGINE emitter; call right after its setEmitter every frame (only needed for upgraded cars).
//   boost:   VehicleState::turboBoost (0..1). The first nonzero value switches the emitter from the engine kind's
//            built-in turbo sound (if any) to the simulation's boost: a whistle that rises in pitch and level while
//            boost builds and winds down after, a blow-off hiss with compressor flutter when the throttle closes at
//            high boost, and a short chirp when boost dumps in a gear change. Cars without the turbo pass 0.
//   tune:    0 stock .. 1 fully tuned, e.g. Saturate(Max(turbo ? 0.6f : 0.f, (engine + transmission) / 6.f)); makes the
//            turbo louder and enables exhaust pops on hard shifts.
//   shifted: VehicleState::shifted. A shift with the throttle down near the top of the rev range pops the exhaust of a
//            tuned car (now and then a double pop).
void setEngineTune(EmitterHandle h, float boost, float tune, bool shifted);
void destroyEmitter(EmitterHandle h);

// Radio: stations play "live" (their timeline advances even while not listened to).
int radioStationCount();
const char* radioStationName(int station);
const char* radioStationGenre(int station);
void setRadioStation(int station);  // -1 = off; plays as the in-car radio (2D, slightly filtered)
int radioStation();
// Current track info for the HUD ("Artist - Title"); empty during DJ talk/ads.
std::string radioNowPlaying(int station);
void setRadioInterior(float amount);  // 1 = inside the car cabin (full), 0 = heard from outside (muffled)

// Speech (procedural formant TTS). Plays asynchronously; positional if pos given.
SoundHandle speak(const char* text, const VoiceParams& voice, float volume = 1.f);
SoundHandle speakAt(const char* text, const VoiceParams& voice, vec3 pos, float volume = 1.f);
float estimateSpeechDuration(const char* text, const VoiceParams& voice);

// Score: dynamic mission music (intensity 0 = off .. 1 = full action), mood seed per mission.
void setScore(int moodSeed, float intensity);

// Debug / tests: render N seconds of the full mix offline into a buffer (no device needed).
void renderOffline(float* outStereo, int frames);

}  // namespace Audio
