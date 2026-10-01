// Internal declarations shared by the audio implementation files (sfx, emitters, ambience, music,
// radio, mixer, output backend). Not part of the public API.
#pragma once
#include "audio.h"
#include "speech.h"
#include "dsp.h"
#include "../core/rng.h"
#include <thread>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <array>
#include <new>

namespace Audio {
namespace detail {

using dsp::kSR;
using dsp::kInvSR;
constexpr int kSampleRate = dsp::kSampleRate;
constexpr int kMaxBlock = 256;  // mixer sub-block size (frames)

// ---------------------------------------------------------------------------------------------
// Firearms: weapon classes and the distant-shot groups shared between them (sfx_guns.cpp, Mixer::startGunshot).
enum GunClass : int { GC_PISTOL = 0, GC_REVOLVER, GC_SMG, GC_RIFLE, GC_SHOTGUN, GC_SNIPER, GC_COUNT };
enum GunFar : int { GF_LIGHT = 0, GF_HEAVY, GF_BIG, GF_COUNT };

// Sound bank: every public Sfx plus internal one-shots used by ambience / crowds / gunfire layers.
enum BankId : int {
    AMB_CRICKET_CHIRP = SFX_COUNT,
    AMB_TREEFROG,
    AMB_BULLFROG,
    AMB_OWL,
    AMB_BIRD_SONG,
    AMB_HERON,
    AMB_HORN_DISTANT,
    AMB_SIREN_DISTANT,
    AMB_BUBBLES,
    AMB_DOG_DISTANT,
    AMB_WATER_LAP,
    AMB_CROW,
    AMB_AIRBOAT,        // stereo pass-bys (2D, baked pan sweep)
    AMB_BOAT_PASS,
    AMB_TRAIN_PASS,
    AMB_CRANE,          // port
    AMB_CONTAINER,
    AMB_REVERSE_BEEP,
    AMB_CONSTRUCTION,   // downtown by day
    AMB_PIG_FROG,       // wetland
    AMB_CHORUS_FROG,
    AMB_LIMPKIN,
    AMB_BLACKBIRD,
    AMB_MOSQUITO,       // stereo, past the ear
    AMB_HALYARD,        // marina
    AMB_BUOY_BELL,
    AMB_THUNDER_CLOSE,  // stereo thunder by distance (Audio::playThunder)
    AMB_THUNDER_MID,
    AMB_THUNDER_FAR,
    GUN_NEAR,                                 // + GunClass: close report (dry: the mixer adds the environment)
    GUN_FP = GUN_NEAR + GC_COUNT,             // + GunClass: the shooter's own perspective (stereo, tight)
    GUN_MECH = GUN_FP + GC_COUNT,             // + GunClass: action cycling (slide / bolt / pump), heard up close
    GUN_SUP = GUN_MECH + GC_COUNT,            // + GunClass: suppressed report
    GUN_FAR_URBAN = GUN_SUP + GC_COUNT,       // + GunFar: distant shot among buildings (rolling slap echoes)
    GUN_FAR_OPEN = GUN_FAR_URBAN + GF_COUNT,  // + GunFar: distant shot over open country (boom + long rumble)
    GUN_CRACK = GUN_FAR_OPEN + GF_COUNT,      // supersonic bullet crack (N-wave) near the bullet path
    STEP_HEEL,                                // + Footwear: heel strike on hard ground (Mixer::startFootstep)
    STEP_TOE = STEP_HEEL + FOOTWEAR_COUNT,    // + Footwear: roll-off and toe (sandals: the flap against the heel)
    STEP_SOFT = STEP_TOE + FOOTWEAR_COUNT,    // any sole on soft ground (grass, dirt, sand, mud)
    STEP_TEX,                                 // + FootSurface: the surface's own sound
    STEP_PUDDLE = STEP_TEX + FOOT_SURFACE_COUNT,  // splash on wet hard ground
    STEP_CLOTH_WALK,                          // clothing per step
    STEP_CLOTH_RUN,
    STEP_GEAR,                                // keys, coins, equipment
    STEP_LAND_HARD,                           // both feet after a jump or a drop
    STEP_LAND_SOFT,
    STEP_SCUFF,                               // sole twisting on the ground (sharp stop / turn)
    BODY_THUD_HARD,                           // a body hitting asphalt / concrete
    BODY_THUD_SOFT,                           // grass, dirt, sand, mud
    BODY_THUD_WOOD,
    BODY_THUD_METAL,
    BODY_SLAP,                                // an arm or leg slapping down
    FOLEY_CLOTH_BURST,                        // vault / climb / dive into cover
    FOLEY_GRAB_HAND,                          // a palm slapping onto a ledge
    BANK_COUNT
};

enum class Bus : u8 { World = 0, Ui = 1, Voice = 2, Music = 3 };

struct SoundDef {
    const char* name;
    Bus bus;
    u8 priority;     // 0..255 (higher survives voice stealing)
    u8 variations;   // number of pre-rendered variations
    float gain;      // loudness trim applied at playback
    float refDist;   // distance with full volume (m)
    float maxDist;   // inaudible beyond (m)
    float reverb;    // reverb send
    float pitchVar;  // random playback pitch variation (+- fraction)
    float airAbs;    // air absorption scale (0 = none, 1 = normal)
};

struct SoundBuffer {
    std::vector<i16> data;  // 16-bit PCM, interleaved when channels == 2
    int channels = 1;
    int frames = 0;
    int rateDiv = 1;        // stored at 48 kHz / rateDiv (band-limited sounds: distant thunder, pass-bys, far gunfire)
};

constexpr int kMaxVariations = 6;
struct BankEntry {
    SoundBuffer vars[kMaxVariations];
    int count = 0;
    std::atomic<bool> ready{false};
};

const SoundDef& soundDef(int id);
BankEntry& bankEntry(int id);
bool bankReady(int id);
// Renders one bank entry (all variations). Thread-safe for distinct ids.
void bankRenderEntry(int id);
// Starts background rendering of the whole bank on `threads` worker threads (non-blocking).
void bankStartAsync(int threads);
// Blocks until the whole bank is rendered (starts rendering if not started).
void bankWaitAll();
void bankShutdown();
bool bankStarted();

// ---------------------------------------------------------------------------------------------
// Emitter synthesis (emitters.cpp). Synths are constructed in place inside fixed storage owned by
// the mixer, so they must not allocate.
struct EmitterSynth {
    virtual ~EmitterSynth() {}
    // Parameters from the game (raw, unsmoothed). Called on the audio thread before render().
    virtual void setParams(float p0, float p1, float p2, float p3) = 0;
    // Mono output at the source's own clock (the mixer resamples for doppler / slow motion).
    virtual void render(float* out, int n) = 0;
    // Called when the emitter is released (fade handled by the mixer).
    virtual void release() {}
    // Level of detail hint from the mixer (0 = full, 1 = distant/quiet: cheaper synthesis).
    virtual void setLod(int lod) { (void)lod; }
    // Engine upgrades (Audio::setEngineTune); ignored by other synths.
    virtual void setTune(float boost, float tune, bool shifted) { (void)boost, (void)tune, (void)shifted; }
    // Chassis / cabin state of a road vehicle (Audio::setVehicleAudio); ignored by other synths.
    virtual void setVehicle(const VehicleAudio& v) { (void)v; }
    // Where the listener is relative to the source: cosine of the angle from the vehicle's forward axis to the
    // listener, and whether the listener sits inside this vehicle.
    virtual void setListener(float cosFront, bool inside) { (void)cosFront, (void)inside; }
    // Sounds heard only from inside the vehicle (2D, not spatialized): accumulated into out. Returns false if silent.
    virtual bool renderCabin(float* out, int n) { (void)out, (void)n; return false; }
};

struct EmitterDef {
    float refDist, maxDist, reverb, gain;
    u8 priority;
};
const EmitterDef& emitterDef(EmitterType t);
constexpr size_t kEmitterStorage = 12288;  // bytes of in-place storage per emitter slot
// Constructs the synth for `type` in `storage` (placement new). Returns nullptr for types handled by
// the mixer itself (EMIT_RADIO_WORLD).
EmitterSynth* constructEmitterSynth(EmitterType type, void* storage, u32 seed);

// ---------------------------------------------------------------------------------------------
// Mixer services used by ambience / crowd code (audio thread only).
void spawnWorldOneShot(int bankId, vec3 worldPos, float volume, float pitch);
// A 2D world one-shot (stereo ambience pass-bys, thunder): flip swaps the channels.
void spawnAmbient2D(int bankId, float volume, float pitch, bool flip);
struct ListenerState {
    vec3 pos, vel, forward = vec3(0, 1, 0), up = vec3(0, 0, 1), right = vec3(1, 0, 0);
    float interior = 0, inVehicle = 0;
    float bodySpeed = -1.f;
};

// ---------------------------------------------------------------------------------------------
// Environment acoustics (acoustics.cpp). The probe runs on the game thread inside update(); the mixer consumes the
// derived AcousticState (reverb zones, early-reflection geometry, open-field echo) and uses it for occlusion.
namespace acoustics {
constexpr int kProbeH = 16;          // horizontal rays (22.5 degree steps)
constexpr int kProbeE = 8;           // elevated rays (40 degrees up, 45 degree steps)
constexpr int kProbeRays = kProbeH + kProbeE + 1;  // + one straight up
constexpr float kProbeHMax = 80.f, kProbeEMax = 60.f, kProbeUpMax = 60.f;
constexpr float kProbeElev = 0.6981317f;  // 40 degrees
}  // namespace acoustics

struct AcousticState {
    float h[acoustics::kProbeH];     // horizontal hit distances (kProbeHMax = nothing within range)
    float e[acoustics::kProbeE];     // elevated hit distances
    float up = acoustics::kProbeUpMax;
    bool probed = false;             // geometry came from rays (false: fallback from the interior / urban hints)
    float enclosed = 0.f;            // 0 open sky .. 1 room / tunnel
    float canyon = 0.f;              // street canyon between facades (outdoors)
    float cover = 0.f;               // roof / deck overhead
    float rtIn = 0.8f, dampIn = 0.3f, wetIn = 0.f, preIn = 0.008f;   // enclosed reverb
    float rtOut = 1.1f, dampOut = 0.55f, wetOut = 0.35f;             // outdoor reverb
    float er = 0.5f;                 // early-reflection level
    int flutterA = -1;               // flutter axis: probe rays (flutterA, flutterA + kProbeH / 2)
    float flutterDelay = 0.f, flutterFb = 0.f;
    float echo = 0.5f;               // open-field far echo level (hills, tree lines, distant facades)
    float urbanFar = 0.5f;           // distant gunfire flavour: 0 open country .. 1 among buildings
    float sendScale = 1.f;           // overall reverb send scale for the environment
    AcousticState() {
        for (float& v : h) v = acoustics::kProbeHMax;
        for (float& v : e) v = acoustics::kProbeEMax;
    }
};

// ---------------------------------------------------------------------------------------------
// Ambience beds (ambience.cpp). Audio thread only.
struct AmbienceRenderer;
AmbienceRenderer* ambienceCreate();
void ambienceDestroy(AmbienceRenderer* a);
void ambienceRender(AmbienceRenderer* a, float* L, float* R, int n, const Ambience& target,
                    const ListenerState& ls, const AcousticState& ac);

// ---------------------------------------------------------------------------------------------
// Crowd walla beds (crowd.cpp): rendered on a background thread at init (or on demand offline).
void crowdStart(bool async);
void crowdStop();

// ---------------------------------------------------------------------------------------------
// Speech jobs (radio.cpp owns the worker). Thread-safe.
struct SpeechJob {
    std::string text;
    VoiceParams voice;
    int sampleRate = 48000;
    int priority = 1;  // 0 = dialogue (urgent), 1 = radio now, 2 = radio lookahead
    std::vector<float> pcm;
    std::atomic<int> state{0};  // 0 queued, 1 running, 2 done
    // Dialogue playback info (priority 0 jobs only)
    SoundHandle handle = 0;
    float volume = 1.f;
    bool positional = false;
    vec3 pos;
    std::atomic<int> refs{1};
};
void speechJobRelease(SpeechJob* j);  // decrements refs, deletes at zero
// Queues a job. In async mode the worker thread synthesizes it; otherwise it is synthesized
// synchronously by speechPumpSync() (offline rendering).
void speechSubmit(SpeechJob* j);
void speechPumpSync();          // offline: synthesize everything queued now
bool speechAsync();
void speechStart(bool async);   // starts the worker thread if async
void speechStop();
void speechCalibrate(float estimated, float actual);  // running duration-estimate correction
float speechDurationScale();
// Called (from the speech worker or speechPumpSync) when a dialogue job (priority 0) finished
// synthesis; the mixer takes its own reference. Implemented in audio.cpp.
void dialogSpeechReady(SpeechJob* j);
// Called when a queued dialogue job is dropped (queue overflow) so its handle stops "playing".
void dialogSpeechDropped(SpeechJob* j);

// ---------------------------------------------------------------------------------------------
// Music producers (music.cpp + radio.cpp): stations and score render ahead into ring buffers on the
// music thread (async mode) or synchronously (offline mode).
constexpr int kProdBlock = 512;
constexpr int kRingBlocks = 32;
constexpr int kStationSlots = 3;
constexpr int kScoreSlot = kStationSlots;
constexpr int kProducerSlots = kStationSlots + 1;

struct ProducerRing {
    struct Block {
        float l[kProdBlock];
        float r[kProdBlock];
        u32 gen;
    };
    Block blocks[kRingBlocks];
    std::atomic<u32> writeCount{0}, readCount{0};
    int readOffset = 0;  // consumer-only
    int filled() const { return (int)(writeCount.load(std::memory_order_acquire) - readCount.load(std::memory_order_acquire)); }
    int freeBlocks() const { return kRingBlocks - filled(); }
};

struct ProducerSlot {
    std::atomic<int> wantedStation{-1};  // station index, or mood seed for the score slot
    std::atomic<u32> wantedGen{0};
    std::atomic<float> intensity{0.f};   // score only
    std::atomic<bool> active{false};     // producer should render
    ProducerRing ring;
};
ProducerSlot& producerSlot(int i);

void musicInit(u64 sessionSeed);
void musicShutdown();
void musicStartThread();
void musicStopThread();
bool musicThreadRunning();
// Offline: render synchronously until every active slot has >= framesNeeded frames queued.
void musicPumpSync(int framesNeeded);
// Radio clock (frames rendered by the mixer since init) - published by the mixer.
extern std::atomic<i64> g_radioFrames;

// Stations (radio.cpp)
int stationCount();
const char* stationName(int i);
const char* stationGenre(int i);
std::string stationNowPlaying(int i, double radioTimeSec);
void radioSetContext(float timeOfDay, float rain);

// Output backend (wasapi.cpp)
typedef void (*RenderCallback)(float* interleavedStereo48k, int frames);
namespace backend {
bool start(RenderCallback cb);  // blocks until device init result is known
void stop();
bool running();
void setThreadHighPriority();   // for the music/speech threads
void setThreadLowPriority();
}  // namespace backend

// Crowd synth one-shot polling (screams); returns bank id or -1.
int emitterPollOneShot(EmitterType type, EmitterSynth* s);

// Misc helpers
FORCEINLINE float randSym(u32& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (float)(i32)s * (1.f / 2147483648.f);
}

}  // namespace detail
}  // namespace Audio
