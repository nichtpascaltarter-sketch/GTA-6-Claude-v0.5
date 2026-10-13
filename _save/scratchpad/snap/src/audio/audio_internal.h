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
// Sound bank: every public Sfx plus internal one-shots used by ambience / crowds.
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
struct ListenerState {
    vec3 pos, vel, forward = vec3(0, 1, 0), up = vec3(0, 0, 1), right = vec3(1, 0, 0);
    float interior = 0, inVehicle = 0;
};

// ---------------------------------------------------------------------------------------------
// Ambience beds (ambience.cpp). Audio thread only.
struct AmbienceRenderer;
AmbienceRenderer* ambienceCreate();
void ambienceDestroy(AmbienceRenderer* a);
void ambienceRender(AmbienceRenderer* a, float* L, float* R, int n, const Ambience& target,
                    const ListenerState& ls);

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
