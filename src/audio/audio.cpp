// Software mixer + public Audio API. The mixer runs on the output thread (WASAPI) or synchronously in
// renderOffline(): commands from the game thread arrive through a short-locked double buffer; voices
// and emitters live in pre-allocated pools; 3D sources get distance attenuation, equal-power panning
// with interaural time/level differences and head shadow, rear/air-absorption/occlusion low-pass,
// doppler (variable-rate resampling), and reverb sends. Buses: world SFX, ambience, UI, dialogue and
// music (radio + score), with speech ducking, loud-event ducking, an FDN reverb and a lookahead
// master limiter.
#include "audio_internal.h"

namespace Audio {
namespace detail {
namespace mix {

using namespace dsp;

constexpr int kMaxVoices = 192;
constexpr int kMaxRealVoices = 64;
constexpr int kMaxEmitters = 512;
constexpr int kMaxRealEmitters = 14;
constexpr float kInaudible = 0.0015f;  // ~-56 dB: masked by any bed; not worth synthesizing
constexpr int kHandleTable = 4096;
constexpr int kItdLen = 64;
constexpr float kSpeedOfSound = 343.f;

// Optional stage timers (define AUDIO_PROFILE to enable; used by the native benchmark).
#ifdef AUDIO_PROFILE
double g_prof[8] = {};
struct ProfScope {
    int k;
    double t0;
    explicit ProfScope(int kk) : k(kk), t0(TimeSeconds()) {}
    ~ProfScope() { g_prof[k] += TimeSeconds() - t0; }
};
#define PROF_SCOPE(k) ProfScope _prof_##k(k)
#else
#define PROF_SCOPE(k) \
    do {              \
    } while (0)
#endif

// ---------------------------------------------------------------------------------------------
enum class CmdType : u8 { Play, Gunshot, Thunder, Stop, EmitterCreate, EmitterSet, EmitterTune, EmitterVehicle, EmitterDestroy, SpeechReady };
struct Cmd {
    CmdType type;
    bool is2D;
    i32 id;
    u32 handle;
    vec3 pos, vel;
    float p[4];
    float volume, pitch;
    SpeechJob* job;
    VehicleAudio va;
};

struct SharedState {
    ListenerState listener;
    bool listenerSet = false;
    Ambience amb;
    float master = 1.f, sfx = 1.f, music = 1.f, voice = 1.f;
    bool paused = false;
    float slowmo = 1.f;
    int radioStation = -1;
    float radioInterior = 1.f;
    int scoreMood = -1;
    float scoreIntensity = 0.f;
    float crowdDensity = 0.f;
    int crowdPlace = 0;
    float crowdPanic = 0.f;
    float duck = 0.62f;           // how far music/radio drop under dialogue (settings: music ducking)
    AcousticState acoustic;       // reverb zones / early-reflection geometry from the probe (or the fallback)
};

std::mutex g_cmdMutex;
std::vector<Cmd> g_pending, g_processing;
SharedState g_shared;
constexpr size_t kMaxPendingCmds = 16384;

std::atomic<u32> g_handleState[kHandleTable];
std::atomic<u32> g_nextHandle{1};
std::atomic<u8> g_emitterBusy[kMaxEmitters];
u32 g_emitterGen[kMaxEmitters];
// occlusion of each emitter slot from the game thread's rays (valid while the handle matches)
std::atomic<u32> g_emOcclHandle[kMaxEmitters];
std::atomic<float> g_emOccl[kMaxEmitters];

// garbage (audio thread -> game thread)
constexpr u32 kGarbageCap = 1024;
SpeechJob* g_garbage[kGarbageCap];
std::atomic<u32> g_garbageW{0}, g_garbageR{0};

static void garbagePush(SpeechJob* j) {
    u32 w = g_garbageW.load(std::memory_order_relaxed);
    if (w - g_garbageR.load(std::memory_order_acquire) >= kGarbageCap) {
        speechJobRelease(j);  // ring full: release here (rare)
        return;
    }
    g_garbage[w % kGarbageCap] = j;
    g_garbageW.store(w + 1, std::memory_order_release);
}
static void garbageDrain() {
    u32 r = g_garbageR.load(std::memory_order_relaxed);
    u32 w = g_garbageW.load(std::memory_order_acquire);
    while (r != w) {
        speechJobRelease(g_garbage[r % kGarbageCap]);
        r++;
    }
    g_garbageR.store(r, std::memory_order_release);
}

FORCEINLINE void handleClear(u32 h) {
    if (!h) return;
    u32 expect = h;
    g_handleState[h & (kHandleTable - 1)].compare_exchange_strong(expect, 0u);
}
FORCEINLINE bool handleAlive(u32 h) { return h && g_handleState[h & (kHandleTable - 1)].load(std::memory_order_acquire) == h; }

// ---------------------------------------------------------------------------------------------
// Spatializer
struct SpatialParams {
    float gl = 0, gr = 0;     // ear gains
    float itdL = 0, itdR = 0; // delay (samples) per ear
    float farCut = 20000.f;   // head-shadow cutoff on the far ear
    int farEar = 0;           // 0 left, 1 right
    float cutoff = 20000.f;   // mono low-pass (air, rear, occlusion)
    float send = 0.f;         // reverb send
    float er = 0.f;           // early-reflection send (probe geometry)
    float echo = 0.f;         // open-field far echo send (impulsive sources)
    float doppler = 1.f;
    float audibility = 0.f;
};

struct SpatialState {
    Svf lp;
    float cutoff = 20000.f;
    float itd[kItdLen] = {};
    int itdW = 0;
    float itdL = 0, itdR = 0;
    OnePoleLP farL, farR;
    float gl = 0, gr = 0, send = 0, er = 0, echo = 0;
    float doppler = 1.f;
    bool init = false;
    void reset() {
        lp.reset();
        memset(itd, 0, sizeof(itd));
        itdW = 0;
        farL.reset();
        farR.reset();
        init = false;
    }
};

struct WorldEnv {
    float inVehicle = 0, interior = 0, underwater = 0, slowmo = 1;
    float sendScale = 1.f;    // environment reverb send scale (canyon / enclosed spaces reverberate more)
};

// occl: 0 clear line of sight .. 1 behind a building / wall. erK / echoK: per-source send weights.
static void computeSpatial(const ListenerState& L, const WorldEnv& env, vec3 pos, vec3 vel, float refDist, float maxDist,
                           float airAbs, float reverbAmt, float occl, float erK, float echoK, SpatialParams& o) {
    vec3 rel = pos - L.pos;
    float d = length(rel);
    vec3 dir = d > 1e-3f ? rel / d : L.forward;
    float x = dot(dir, L.right), y = dot(dir, L.forward);
    // distance attenuation (inverse distance beyond refDist, smooth fade to zero near maxDist)
    float g = d <= refDist ? 1.f : refDist / d;
    g *= 1.f - SmoothStep(maxDist * 0.55f, maxDist, d);
    // panning: collapse to center for very close sources
    float closeK = SmoothStep(0.2f, 1.5f, d);
    float pan = x * closeK;
    float a = (0.5f + 0.5f * pan * 0.9f) * kHalfPi;
    o.gl = cosf(a) * g * 1.41421f;
    o.gr = sinf(a) * g * 1.41421f;
    // interaural time difference (up to ~0.65 ms) on the far ear
    float itd = fabsf(pan) * 0.00065f * kSR;
    o.itdL = pan > 0.f ? itd : 0.f;
    o.itdR = pan < 0.f ? itd : 0.f;
    o.farEar = pan > 0.f ? 0 : 1;
    o.farCut = 20000.f - 16500.f * powf(fabsf(pan), 1.5f);
    // mono low-pass: rear shadow, air absorption, enclosure, vehicle, underwater, slow motion, occlusion
    float fcRear = y < 0.f ? Lerp(20000.f, 7500.f, -y * closeK) : 20000.f;
    float fcAir = 22000.f / (1.f + d * airAbs / 60.f);
    float fcInt = Lerp(22000.f, 2600.f, env.interior * SmoothStep(8.f, 30.f, d));
    float fcVeh = Lerp(22000.f, 1100.f, env.inVehicle);
    float fcUw = Lerp(22000.f, 420.f, env.underwater);
    float fcSlow = Lerp(4500.f, 22000.f, SmoothStep(0.3f, 1.f, env.slowmo));
    // behind a building the direct sound only arrives diffracted round the edges: dull and quieter
    float fcOcc = occl > 0.f ? 22000.f * powf(0.034f, occl) : 22000.f;  // -> ~750 Hz fully occluded
    o.cutoff = Min(Min(Min(fcRear, fcAir), Min(fcInt, fcVeh)), Min(Min(fcUw, fcSlow), fcOcc));
    float muffle = Lerp(1.f, 0.45f, env.inVehicle) * Lerp(1.f, 0.6f, env.underwater);
    float occG = Lerp(1.f, 0.3f, occl);
    o.gl *= muffle * occG;
    o.gr *= muffle * occG;
    // sends: the reverberant field grows relative to the direct sound with distance and enclosure; an occluded
    // source still excites the space around the listener (its reflections come round the corner)
    float base = reverbAmt * sqrtf(Max(g, 0.f)) * muffle * Lerp(1.f, 0.75f, occl);
    float farK = 0.45f + 0.55f * SmoothStep(3.f, 80.f, d);
    o.send = base * farK * env.sendScale;
    o.er = base * erK * Lerp(1.f, 2.2f, SmoothStep(4.f, 90.f, d));
    o.echo = base * echoK;
    // doppler
    float vl = dot(L.vel, dir), vs = dot(vel, dir);
    float dop = (kSpeedOfSound + vl) / Max(kSpeedOfSound + vs, 30.f);
    o.doppler = Clamp(dop, 0.5f, 2.f);
    o.audibility = g * muffle * occG;
}

// Mono send buses fed by every source (reverb, early reflections, far echo).
struct SendBus {
    float* rev;
    float* er;
    float* echo;
};

// Processes a mono source block through the spatial state into L/R (+ environment sends).
static void spatialize(SpatialState& st, const SpatialParams& p, const float* in, float* outL, float* outR, const SendBus& sb,
                       int n, float gain, bool fadeIn) {
    if (!st.init) {
        st.gl = p.gl * gain;
        st.gr = p.gr * gain;
        st.send = p.send * gain;
        st.er = p.er * gain;
        st.echo = p.echo * gain;
        st.itdL = p.itdL;
        st.itdR = p.itdR;
        st.cutoff = p.cutoff;
        st.lp.setG(svfG(p.cutoff), 0.707f);
        st.init = true;
        if (fadeIn) st.gl = st.gr = st.send = st.er = st.echo = 0.f;
    }
    // smooth parameter targets across blocks (reduces zipper from game-rate position updates)
    float tgl = p.gl * gain, tgr = p.gr * gain, ts = p.send * gain, te = p.er * gain, tc = p.echo * gain;
    float gl0 = st.gl, gr0 = st.gr, s0 = st.send, e0 = st.er, c0 = st.echo;
    float gl1 = gl0 + (tgl - gl0) * 0.5f, gr1 = gr0 + (tgr - gr0) * 0.5f, s1 = s0 + (ts - s0) * 0.5f;
    float e1 = e0 + (te - e0) * 0.5f, c1 = c0 + (tc - c0) * 0.5f;
    if (fadeIn) {
        gl1 = tgl * 0.5f;
        gr1 = tgr * 0.5f;
    }
    st.cutoff += (p.cutoff - st.cutoff) * 0.5f;
    st.lp.setG(svfG(Clamp(st.cutoff, 40.f, 20000.f)), 0.707f);
    float farC = onePoleCoef(p.farCut);
    st.farL.setCoef(p.farEar == 0 ? farC : 1.f);
    st.farR.setCoef(p.farEar == 1 ? farC : 1.f);
    float dL0 = st.itdL, dR0 = st.itdR;
    float dL1 = dL0 + (p.itdL - dL0) * 0.5f, dR1 = dR0 + (p.itdR - dR0) * 0.5f;
    float invN = 1.f / (float)n;
    bool bypassLp = st.cutoff > 19000.f;
    bool doEcho = c0 > 1e-6f || c1 > 1e-6f;
    if (p.audibility * gain < 0.012f && gl0 + gr0 < 0.03f) {
        // faint source: plain gains, no interaural delay or head shadow (inaudible at this level)
        for (int i = 0; i < n; i++) {
            float u = (float)i * invN;
            float x = in[i];
            if (!bypassLp) x = st.lp.lp(x);
            st.itd[st.itdW] = x;  // keep the interaural history current for a later switch to the full path
            st.itdW = (st.itdW + 1) & (kItdLen - 1);
            outL[i] += x * (gl0 + (gl1 - gl0) * u);
            outR[i] += x * (gr0 + (gr1 - gr0) * u);
            sb.rev[i] += x * (s0 + (s1 - s0) * u);
            sb.er[i] += x * (e0 + (e1 - e0) * u);
            if (doEcho) sb.echo[i] += x * (c0 + (c1 - c0) * u);
        }
        st.gl = gl1;
        st.gr = gr1;
        st.send = s1;
        st.er = e1;
        st.echo = c1;
        st.itdL = dL1;
        st.itdR = dR1;
        return;
    }
    for (int i = 0; i < n; i++) {
        float u = (float)i * invN;
        float x = in[i];
        if (!bypassLp) x = st.lp.lp(x);
        st.itd[st.itdW] = x;
        float dl = dL0 + (dL1 - dL0) * u, dr = dR0 + (dR1 - dR0) * u;
        auto rd = [&](float dd) {
            if (dd < 0.01f) return x;
            int di = (int)dd;
            float fr = dd - (float)di;
            float a = st.itd[(st.itdW - di) & (kItdLen - 1)];
            float b = st.itd[(st.itdW - di - 1) & (kItdLen - 1)];
            return a + (b - a) * fr;
        };
        float xl = rd(dl), xr = rd(dr);
        st.itdW = (st.itdW + 1) & (kItdLen - 1);
        if (p.farEar == 0) xl = st.farL.process(xl);
        else xr = st.farR.process(xr);
        float gl = gl0 + (gl1 - gl0) * u, gr = gr0 + (gr1 - gr0) * u;
        outL[i] += xl * gl;
        outR[i] += xr * gr;
        sb.rev[i] += x * (s0 + (s1 - s0) * u);
        sb.er[i] += x * (e0 + (e1 - e0) * u);
        if (doEcho) sb.echo[i] += x * (c0 + (c1 - c0) * u);
    }
    st.gl = gl1;
    st.gr = gr1;
    st.send = s1;
    st.er = e1;
    st.echo = c1;
    st.itdL = dL1;
    st.itdR = dR1;
}

// ---------------------------------------------------------------------------------------------
struct Voice {
    bool active = false;
    u32 handle = 0;
    int id = 0;
    const SoundBuffer* buf = nullptr;
    SpeechJob* speech = nullptr;
    double pos = 0.0;
    float rate = 1.f;
    float volume = 1.f;
    Bus bus = Bus::World;
    bool is3D = false;
    vec3 pos3;
    float refDist = 1, maxDist = 100, reverb = 0, airAbs = 1;
    u8 priority = 100;
    bool real = false, wasReal = false;
    float score = 0.f;
    bool stopping = false;
    float stopGain = 1.f;
    SpatialState sp;
    SpatialParams par;
    int age = 0;
    int delay = 0;           // samples until the sound arrives (speed of sound)
    float erK = 1.f;         // early-reflection send weight
    float echoK = 0.f;       // open-field far echo send weight (gunfire, explosions)
    float occl = 0.f;        // occlusion 0..1 (fixed for the life of a one-shot)
    bool fp = false;         // first-person gun layer: 2D, not ducked by the player-shot duck
    float pan2D = 0.f;       // 2D world voices: stereo placement
    bool flip = false;       // 2D stereo buffers: swap channels (ambience pass-bys from the other side)
};

struct Emitter {
    bool active = false;
    u32 handle = 0;
    EmitterType type = EMIT_ENGINE;
    EmitterSynth* synth = nullptr;
    alignas(16) u8 storage[kEmitterStorage];
    vec3 pos, vel;
    float p[4] = {0, 0, 0, 0};
    float volume = 1.f, volSmooth = 0.f;
    bool releasing = false;
    bool real = false, wasReal = false;
    float score = 0.f;
    float fifo[320];
    int fifoLen = 0;
    double fifoPos = 1.0;
    SpatialState sp;
    SpatialParams par;
    Biquad spkHp, spkLp;  // world radio speaker coloration
    DCBlocker dc;
    u32 seed = 0;
    float occl = 0.f;     // smoothed occlusion
    vec3 fwd = vec3(0, 1, 0);  // road vehicles: heading (intake / exhaust balance)
    bool player = false;       // the player's own vehicle (cabin sounds)
    bool vehicle = false;      // receives setVehicleAudio
};

struct Mixer {
    Voice voices[kMaxVoices];
    Emitter* emitters = nullptr;
    SharedState st;
    ListenerState lis;
    WorldEnv env;
    AmbienceRenderer* amb = nullptr;
    crowd::Player crowdPlayer;
    acoustics::EnvFx envfx;
    LookaheadLimiter limiter;
    Noise nz{0x77u};
    float pauseGain = 1.f;
    float slowmo = 1.f;
    // smoothed bus volumes
    float vMaster = 1, vSfx = 1, vMusic = 1, vVoice = 1;
    // ducking
    EnvFollower speechEnv, loudEnv;
    float speechDuck = 1.f, loudDuck = 1.f;
    // player-shot duck: the rest of the world dips for a moment under the player's own gunfire
    float fpDuckEnv = 0.f, fpDuck = 1.f;
    // radio
    int radioStation = -1;
    int slotOfStation[kStationSlots];
    u32 slotGen[kStationSlots];
    int slotStation[kStationSlots];
    float slotUnused[kStationSlots];
    float slotL[kProducerSlots][kMaxBlock], slotR[kProducerSlots][kMaxBlock];
    int slotGot[kProducerSlots];
    float staticTime = 0.f, staticMin = 0.f;
    float radioGain = 0.f;
    float radioCut = 12000.f, radioInt = 1.f;
    Biquad carHpL, carHpR, carBoomL, carBoomR;
    Svf carLpL, carLpR;
    Svf staticBp;
    float staticWhistle = 0.f, staticCrackle = 0.f;
    float scoreFade = 0.f;
    // buffers
    float wL[kMaxBlock], wR[kMaxBlock];      // world sfx
    float wmL[kMaxBlock], wmR[kMaxBlock];    // world music (radio emitters)
    float aL[kMaxBlock], aR[kMaxBlock];      // ambience
    float cL[kMaxBlock], cR[kMaxBlock];      // crowd walla
    float uL[kMaxBlock], uR[kMaxBlock];      // ui
    float vL[kMaxBlock], vR[kMaxBlock];      // dialogue
    float mL[kMaxBlock], mR[kMaxBlock];      // music (radio + score)
    float fpL[kMaxBlock], fpR[kMaxBlock];    // first-person gun layers
    float sRev[kMaxBlock], sEr[kMaxBlock], sEcho[kMaxBlock];  // environment sends (mono)
    float rL[kMaxBlock], rR[kMaxBlock];      // environment return
    float mono[kMaxBlock];
    float mono2[kMaxBlock];
    Rng rng{0xA0D10u};
    DCBlocker ambDcL, ambDcR, masterDcL, masterDcR;
    bool initialized = false;
    int blockCounter = 0;

    void init() {
        if (initialized) return;
        emitters = new Emitter[kMaxEmitters];
        amb = ambienceCreate();
        envfx.init();
        limiter.init(72, 0.95f, 120.f);
        speechEnv.set(0.02f, 0.45f);
        ambDcL.r = ambDcR.r = 0.999f;
        masterDcL.r = masterDcR.r = 0.9996f;
        loudEnv.set(0.004f, 0.8f);
        for (int i = 0; i < kStationSlots; i++) {
            slotStation[i] = -1;
            slotGen[i] = 0;
            slotUnused[i] = 0.f;
        }
        carHpL.setHP(70.f, 0.7f);
        carHpR = carHpL;
        carBoomL.setPeak(110.f, 1.f, 2.5f);
        carBoomR = carBoomL;
        staticBp.set(2500.f, 1.2f);
        st = SharedState();
        initialized = true;
    }
    void destroy() {
        if (!initialized) return;
        for (auto& v : voices) freeVoice(v, true);
        for (int i = 0; i < kMaxEmitters; i++) freeEmitter(emitters[i], i, true);
        delete[] emitters;
        emitters = nullptr;
        ambienceDestroy(amb);
        amb = nullptr;
        initialized = false;
    }

    // ---- voices
    void freeVoice(Voice& v, bool immediate) {
        if (!v.active) return;
        handleClear(v.handle);
        if (v.speech) {
            if (immediate) speechJobRelease(v.speech);
            else garbagePush(v.speech);
            v.speech = nullptr;
        }
        v.active = false;
    }
    Voice* allocVoice(u8 priority) {
        for (auto& v : voices)
            if (!v.active) return &v;
        // steal the lowest-scoring world voice with lower priority
        Voice* best = nullptr;
        float bs = 1e9f;
        for (auto& v : voices) {
            if (v.bus == Bus::Ui || v.bus == Bus::Voice) continue;
            float s = v.score + (v.priority >= priority ? 1000.f : 0.f);
            if (s < bs) { bs = s; best = &v; }
        }
        if (best && bs < 1000.f) {
            freeVoice(*best, false);
            return best;
        }
        return nullptr;
    }
    // Open-field echo weight of impulsive one-shots (their bangs roll back from tree lines and distant blocks).
    static float echoWeight(int id) {
        switch (id) {
            case SFX_EXPLOSION: case SFX_EXPLOSION_SMALL: return 1.f;
            case SFX_ROCKET_LAUNCH: return 0.8f;
            case SFX_TIRE_POP: return 0.6f;
            case SFX_CAR_CRASH_HEAVY: return 0.5f;
            case SFX_THUNDER: case AMB_THUNDER_CLOSE: case AMB_THUNDER_MID: case AMB_THUNDER_FAR: return 0.4f;
            default: return 0.f;
        }
    }
    static int gunClassOf(int sfx) {
        switch (sfx) {
            case SFX_PISTOL: case SFX_SILENCED: return GC_PISTOL;
            case SFX_REVOLVER: return GC_REVOLVER;
            case SFX_SMG: return GC_SMG;
            case SFX_RIFLE: return GC_RIFLE;
            case SFX_SHOTGUN: return GC_SHOTGUN;
            case SFX_SNIPER: return GC_SNIPER;
            default: return -1;
        }
    }

    // One layer of a composite sound (gunfire): a plain voice with its own bank id, delay and sends.
    Voice* startLayer(int id, u32 handle, vec3 pos, float volume, float pitch, int delay, float erK, float echoK, float occl) {
        if (id <= 0 || id >= BANK_COUNT || !bankReady(id)) return nullptr;
        const SoundDef& d = soundDef(id);
        BankEntry& e = bankEntry(id);
        if (e.count <= 0 || volume <= 1e-4f) return nullptr;
        Voice* v = allocVoice(d.priority);
        if (!v) return nullptr;
        *v = Voice();
        v->active = true;
        v->handle = handle;
        v->id = id;
        v->buf = &e.vars[rng.irange(0, e.count - 1)];
        v->rate = pitch * (1.f + rng.range(-d.pitchVar, d.pitchVar)) / (float)v->buf->rateDiv;
        v->volume = volume * d.gain;
        v->bus = Bus::World;
        v->is3D = true;
        v->pos3 = pos;
        v->refDist = d.refDist;
        v->maxDist = d.maxDist;
        v->reverb = d.reverb;
        v->airAbs = d.airAbs;
        v->priority = d.priority;
        v->delay = delay;
        v->erK = erK;
        v->echoK = echoK;
        v->occl = occl;
        v->sp.reset();
        return v;
    }

    // Gunfire: report (close / first-person / suppressed) + action + distant boom + supersonic crack.
    void startGunshot(int sfx, u32 handle, vec3 pos, vec3 dir, u32 flags, float volume, float pitch, float occlHint = -1.f) {
        int gc = gunClassOf(sfx);
        if (gc < 0) {
            handleClear(handle);
            return;
        }
        bool sup = (flags & GUN_SUPPRESSED) != 0 || sfx == SFX_SILENCED;
        vec3 rel = pos - lis.pos;
        float d = length(rel);
        bool fp = (flags & GUN_PLAYER) != 0 || d < 1.2f;
        int delay = fp ? 0 : (int)(Min(d / kSpeedOfSound, 4.f) * kSR);
        float occ = fp ? 0.f : occlHint >= 0.f ? occlHint : acoustics::probeOcclusion(st.acoustic, rel);
        float pan2D = d > 0.3f ? Clamp(dot(rel / d, lis.right), -1.f, 1.f) * 0.3f : 0.f;
        // report: bystander / shooter perspective / suppressed
        int rid = sup ? GUN_SUP + gc : fp ? GUN_FP + gc : GUN_NEAR + gc;
        float nearW = fp ? 1.f : 1.f - 0.55f * SmoothStep(90.f, 500.f, d);
        Voice* rv = startLayer(rid, handle, pos, volume * nearW, pitch, delay, sup ? 1.2f : 2.4f, sup ? 0.35f : 1.f, occ);
        if (!rv) handleClear(handle);
        else if (fp) {
            rv->fp = true;
            rv->is3D = sup;  // a suppressed shot is quiet enough to stay placed at the muzzle
            rv->pan2D = pan2D;
        }
        // the action cycling (heard up close; it dominates a suppressed shot)
        if (fp || d < 28.f) {
            Voice* mv = startLayer(GUN_MECH + gc, 0, pos, volume * (sup ? 1.5f : 1.f) * (fp ? 1.25f : 1.f), pitch, delay, 0.3f, 0.f, occ);
            if (mv && fp) {
                mv->fp = true;
                mv->is3D = false;
                mv->pan2D = pan2D;
            }
        }
        // distant boom: urban rolling echoes or open-country rumble
        if (!sup || gc == GC_RIFLE || gc == GC_SNIPER || gc == GC_REVOLVER) {
            float farW = SmoothStep(35.f, 220.f, d) * (sup ? 0.3f : 1.f);
            if (farW > 0.01f) {
                int g = gc == GC_PISTOL || gc == GC_SMG ? GF_LIGHT : gc == GC_SNIPER ? GF_BIG : GF_HEAVY;
                float u = Saturate(envfx.cur.urbanFar);
                if (u > 0.15f) startLayer(GUN_FAR_URBAN + g, 0, pos, volume * farW * sqrtf(u), pitch, delay, 0.15f, 0.15f, occ * 0.4f);
                if (u < 0.85f) startLayer(GUN_FAR_OPEN + g, 0, pos, volume * farW * sqrtf(1.f - u), pitch, delay, 0.1f, 0.3f, occ * 0.4f);
            }
        }
        // supersonic crack where the round passes near the listener (the bullet outruns its own report)
        if ((gc == GC_RIFLE || gc == GC_SNIPER || gc == GC_REVOLVER) && !fp && length2(dir) > 0.25f) {
            vec3 dn = normalize(dir);
            float t = dot(lis.pos - pos, dn);
            if (t > 4.f && t < 1000.f) {
                vec3 P = pos + dn * t;
                float miss = length(lis.pos - P);
                if (miss < 30.f) {
                    float vb = gc == GC_SNIPER ? 820.f : gc == GC_RIFLE ? 900.f : 450.f;
                    float tArr = t / vb + miss / kSpeedOfSound;
                    float amp = volume * Clamp(2.4f / powf(miss + 1.f, 0.75f), 0.05f, 1.3f) * (gc == GC_REVOLVER ? 0.5f : 1.f);
                    startLayer(GUN_CRACK, 0, P, amp, pitch * Lerp(1.15f, 0.9f, Saturate(miss / 30.f)), (int)(tArr * kSR), 0.4f, 0.2f,
                               0.f);
                }
            }
        }
    }

    void startSfx(int id, u32 handle, bool is2D, vec3 pos, float volume, float pitch, float occlHint = -1.f) {
        if (gunClassOf(id) >= 0) {
            startGunshot(id, handle, pos, vec3(), is2D ? (u32)GUN_PLAYER : 0u, volume, pitch, occlHint);
            return;
        }
        if (id <= 0 || id >= BANK_COUNT || !bankReady(id)) {
            handleClear(handle);
            return;
        }
        const SoundDef& d = soundDef(id);
        BankEntry& e = bankEntry(id);
        if (e.count <= 0) {
            handleClear(handle);
            return;
        }
        Voice* v = allocVoice(d.priority);
        if (!v) {
            handleClear(handle);
            return;
        }
        *v = Voice();
        v->active = true;
        v->handle = handle;
        v->id = id;
        v->buf = &e.vars[rng.irange(0, e.count - 1)];
        v->pos = 0.0;
        v->rate = pitch * (1.f + rng.range(-d.pitchVar, d.pitchVar)) / (float)v->buf->rateDiv;
        v->volume = volume * d.gain;
        v->bus = d.bus;
        v->is3D = !is2D && d.bus == Bus::World;
        v->pos3 = pos;
        v->refDist = d.refDist;
        v->maxDist = d.maxDist;
        v->reverb = d.reverb;
        v->airAbs = d.airAbs;
        v->priority = d.priority;
        v->echoK = echoWeight(id);
        if (v->is3D) {
            vec3 rel = pos - lis.pos;
            float dist = length(rel);
            v->occl = occlHint >= 0.f ? occlHint : acoustics::probeOcclusion(st.acoustic, rel);
            if (dist > 12.f) v->delay = (int)(Min(dist / kSpeedOfSound, 3.f) * kSR);
        }
        v->sp.reset();
    }
    void startSpeech(SpeechJob* j) {
        if (!handleAlive(j->handle) || j->pcm.empty()) {
            handleClear(j->handle);
            garbagePush(j);
            return;
        }
        Voice* v = allocVoice(200);
        if (!v) {
            handleClear(j->handle);
            garbagePush(j);
            return;
        }
        *v = Voice();
        v->active = true;
        v->handle = j->handle;
        v->speech = j;
        v->rate = (float)j->sampleRate / kSR;
        v->volume = j->volume;
        v->bus = Bus::Voice;
        v->is3D = j->positional;
        v->pos3 = j->pos;
        v->refDist = 2.f;
        v->maxDist = 45.f;
        v->reverb = 0.15f;
        v->airAbs = 1.f;
        v->priority = 220;
        v->erK = 0.8f;
        if (v->is3D) v->occl = acoustics::probeOcclusion(st.acoustic, j->pos - lis.pos);
        v->sp.reset();
    }

    // ---- emitters
    void freeEmitter(Emitter& e, int slot, bool immediate) {
        (void)immediate;
        if (!e.active) return;
        if (e.synth) {
            e.synth->~EmitterSynth();
            e.synth = nullptr;
        }
        e.active = false;
        e.handle = 0;
        g_emitterBusy[slot].store(0, std::memory_order_release);
    }

    // ---- commands
    void processCommands() {
        if (g_cmdMutex.try_lock()) {
            g_processing.swap(g_pending);
            st = g_shared;
            g_cmdMutex.unlock();
        } else {
            return;
        }
        for (const Cmd& c : g_processing) {
            switch (c.type) {
                case CmdType::Play: startSfx(c.id, c.handle, c.is2D, c.pos, c.volume, c.pitch, c.p[1]); break;
                case CmdType::Gunshot: startGunshot(c.id, c.handle, c.pos, c.vel, (u32)c.p[0], c.volume, c.pitch, c.p[1]); break;
                case CmdType::Thunder: {
                    // by distance: a close strike tears before the boom, 1-3 km booms and rolls, farther only rumbles
                    float dist = c.p[0];
                    int id = dist < 1100.f ? AMB_THUNDER_CLOSE : dist < 3200.f ? AMB_THUNDER_MID : AMB_THUNDER_FAR;
                    spawn2D(id, c.volume * Clamp(1.3f - dist / 6000.f, 0.3f, 1.f), rng.range(0.9f, 1.06f), rng.chance(0.5f));
                    break;
                }
                case CmdType::Stop:
                    for (auto& v : voices)
                        if (v.active && v.handle == c.handle) {
                            v.stopping = true;
                            handleClear(v.handle);
                        }
                    break;
                case CmdType::EmitterCreate: {
                    int slot = (int)(c.handle % (u32)kMaxEmitters);
                    Emitter& e = emitters[slot];
                    if (e.active) freeEmitter(e, slot, false);
                    e.active = true;
                    e.handle = c.handle;
                    e.type = (EmitterType)c.id;
                    e.seed = hash32(c.handle * 2654435761u);
                    e.synth = constructEmitterSynth(e.type, e.storage, e.seed);
                    e.pos = c.pos;
                    e.vel = c.vel;
                    for (int k = 0; k < 4; k++) e.p[k] = c.p[k];
                    e.volume = c.volume;
                    e.volSmooth = 0.f;
                    e.releasing = false;
                    e.real = e.wasReal = false;
                    e.fifoLen = 2;
                    e.fifo[0] = e.fifo[1] = 0.f;
                    e.fifoPos = 1.0;
                    e.sp.reset();
                    e.dc.reset();
                    e.fwd = vec3(0, 1, 0);
                    e.player = false;
                    e.vehicle = false;
                    e.spkHp.setHP(130.f, 0.7f);
                    e.spkLp.setLP(6500.f, 0.7f);
                    if (e.synth) e.synth->setParams(e.p[0], e.p[1], e.p[2], e.p[3]);
                    break;
                }
                case CmdType::EmitterSet: {
                    int slot = (int)(c.handle % (u32)kMaxEmitters);
                    Emitter& e = emitters[slot];
                    if (!e.active || e.handle != c.handle || e.releasing) break;
                    e.pos = c.pos;
                    e.vel = c.vel;
                    for (int k = 0; k < 4; k++) e.p[k] = c.p[k];
                    e.volume = c.volume;
                    if (e.synth) e.synth->setParams(e.p[0], e.p[1], e.p[2], e.p[3]);
                    break;
                }
                case CmdType::EmitterTune: {
                    int slot = (int)(c.handle % (u32)kMaxEmitters);
                    Emitter& e = emitters[slot];
                    if (!e.active || e.handle != c.handle || e.releasing) break;
                    if (e.synth) e.synth->setTune(c.p[0], c.p[1], c.p[2] > 0.5f);
                    break;
                }
                case CmdType::EmitterVehicle: {
                    int slot = (int)(c.handle % (u32)kMaxEmitters);
                    Emitter& e = emitters[slot];
                    if (!e.active || e.handle != c.handle || e.releasing) break;
                    if (length2(c.va.forward) > 0.25f) e.fwd = normalize(c.va.forward);
                    e.player = c.va.player;
                    e.vehicle = true;
                    if (e.synth) e.synth->setVehicle(c.va);
                    break;
                }
                case CmdType::EmitterDestroy: {
                    int slot = (int)(c.handle % (u32)kMaxEmitters);
                    Emitter& e = emitters[slot];
                    if (!e.active || e.handle != c.handle) break;
                    e.releasing = true;
                    if (e.synth) e.synth->release();
                    if (!e.real) freeEmitter(e, slot, false);
                    break;
                }
                case CmdType::SpeechReady: startSpeech(c.job); break;
            }
        }
        g_processing.clear();
    }

    // ---- radio slot management
    int slotFor(int station) const {
        for (int i = 0; i < kStationSlots; i++)
            if (slotStation[i] == station) return i;
        return -1;
    }
    void assignSlots(const int* desired, int nd, float dt) {
        bool want[kStationSlots] = {};
        for (int k = 0; k < nd; k++) {
            int s = slotFor(desired[k]);
            if (s >= 0) want[s] = true;
        }
        for (int k = 0; k < nd; k++) {
            if (slotFor(desired[k]) >= 0) continue;
            // free slot: unused or not desired
            int pick = -1;
            for (int i = 0; i < kStationSlots && pick < 0; i++)
                if (slotStation[i] < 0) pick = i;
            for (int i = 0; i < kStationSlots && pick < 0; i++)
                if (!want[i]) pick = i;
            if (pick < 0) continue;
            slotStation[pick] = desired[k];
            slotGen[pick]++;
            want[pick] = true;
            ProducerSlot& ps = producerSlot(pick);
            ps.wantedGen.store(slotGen[pick], std::memory_order_release);
            ps.wantedStation.store(desired[k], std::memory_order_release);
        }
        for (int i = 0; i < kStationSlots; i++) {
            if (slotStation[i] < 0) continue;
            if (want[i]) slotUnused[i] = 0.f;
            else if ((slotUnused[i] += dt) > 2.f) {
                slotStation[i] = -1;
                producerSlot(i).wantedStation.store(-1, std::memory_order_release);
            }
        }
    }
    int readSlot(int slot, float* L, float* R, int n, u32 wantGen, bool checkGen) {
        ProducerRing& ring = producerSlot(slot).ring;
        int got = 0;
        while (got < n) {
            u32 rc = ring.readCount.load(std::memory_order_relaxed);
            u32 wc = ring.writeCount.load(std::memory_order_acquire);
            if (rc == wc) break;
            ProducerRing::Block& b = ring.blocks[rc % kRingBlocks];
            if (checkGen && b.gen != wantGen) {
                ring.readOffset = 0;
                ring.readCount.store(rc + 1, std::memory_order_release);
                continue;
            }
            int take = Min(kProdBlock - ring.readOffset, n - got);
            memcpy(L + got, b.l + ring.readOffset, sizeof(float) * (size_t)take);
            memcpy(R + got, b.r + ring.readOffset, sizeof(float) * (size_t)take);
            ring.readOffset += take;
            got += take;
            if (ring.readOffset >= kProdBlock) {
                ring.readOffset = 0;
                ring.readCount.store(rc + 1, std::memory_order_release);
            }
        }
        for (int i = got; i < n; i++) L[i] = R[i] = 0.f;
        return got;
    }

    // ---- main render (n <= kMaxBlock), interleaved stereo output
    void render(float* out, int n) {
        processCommands();
        const float blockSec = (float)n * kInvSR;
        // listener / environment
        if (st.listenerSet) {
            lis = st.listener;
            vec3 f = normalize(lis.forward), u = normalize(lis.up);
            vec3 r = cross(f, u);
            if (length2(r) < 1e-8f) r = vec3(1, 0, 0);
            lis.forward = f;
            lis.up = u;
            lis.right = normalize(r);
        }
        env.inVehicle = Saturate(st.listener.inVehicle);
        env.interior = Saturate(st.listener.interior);
        env.underwater = Saturate(st.amb.underwater);
        float smTarget = Clamp(st.slowmo, 0.05f, 2.f);
        slowmo += (smTarget - slowmo) * (1.f - expf(-blockSec / 0.25f));
        env.slowmo = slowmo;
        float pk = 1.f - expf(-blockSec / 0.08f);
        pauseGain += ((st.paused ? 0.f : 1.f) - pauseGain) * pk;
        if (st.paused && pauseGain < 0.002f) pauseGain = 0.f;
        float vk = 1.f - expf(-blockSec / 0.05f);
        vMaster += (Saturate(st.master) - vMaster) * vk;
        vSfx += (Saturate(st.sfx) - vSfx) * vk;
        vMusic += (Saturate(st.music) - vMusic) * vk;
        vVoice += (Saturate(st.voice) - vVoice) * vk;
        // environment acoustics: reverb zones and early-reflection geometry from the probe
        envfx.setTarget(st.acoustic, lis, blockSec);
        env.sendScale = envfx.cur.sendScale * (1.f + 0.6f * env.underwater);
        for (int i = 0; i < n; i++) {
            wL[i] = wR[i] = wmL[i] = wmR[i] = aL[i] = aR[i] = uL[i] = uR[i] = cL[i] = cR[i] = 0.f;
            vL[i] = vR[i] = mL[i] = mR[i] = fpL[i] = fpR[i] = 0.f;
            sRev[i] = sEr[i] = sEcho[i] = rL[i] = rR[i] = 0.f;
        }
        bool worldActive = pauseGain > 0.f;
        // ---- ambience
        if (worldActive && amb) {
            PROF_SCOPE(0);
            radioSetContext(st.amb.timeOfDay, st.amb.rain);
            ambienceRender(amb, aL, aR, n, st.amb, lis, st.acoustic);
            for (int i = 0; i < n; i++) {
                aL[i] = ambDcL.process(aL[i]);
                aR[i] = ambDcR.process(aR[i]);
            }
            float crowdD = st.crowdDensity * (1.f - Saturate(st.amb.underwater));
            crowdPlayer.render(cL, cR, n, crowdD, st.crowdPlace, st.crowdPanic, env.interior, env.inVehicle);
        }
        // ---- voices
        {
            PROF_SCOPE(1);
            renderVoices(n, worldActive, blockSec);
        }
        // ---- radio producers (in-car + world radio sources)
        {
            PROF_SCOPE(2);
            renderRadio(n, blockSec);
        }
        // ---- emitters
        if (worldActive) {
            PROF_SCOPE(3);
            renderEmitters(n, blockSec);
        }
        // ---- score
        renderScore(n, blockSec);
        // ---- environment: early reflections, flutter, far echoes, enclosed + outdoor reverb
        {
            PROF_SCOPE(4);
            envfx.process(sRev, sEr, sEcho, rL, rR, n);
        }
        // ---- ducking envelopes
        float sEnvMax = 0.f, lEnvMax = 0.f;
        for (int i = 0; i < n; i++) {
            float v = Max(fabsf(vL[i]), fabsf(vR[i])) * vVoice;
            sEnvMax = Max(sEnvMax, speechEnv.process(v));
            float w = Max(Max(fabsf(wL[i]), fabsf(wR[i])), Max(fabsf(fpL[i]), fabsf(fpR[i]))) * vSfx;
            lEnvMax = Max(lEnvMax, loudEnv.process(w));
        }
        float sd = 1.f - Clamp(st.duck, 0.f, 0.95f) * SmoothStep(0.004f, 0.05f, sEnvMax);
        float ld = SmoothStep(0.3f, 1.2f, lEnvMax);
        float speechDuck0 = speechDuck, loudDuck0 = loudDuck, fpDuck0 = fpDuck;
        speechDuck += (sd - speechDuck) * (sd < speechDuck ? 0.5f : 0.08f);
        loudDuck += ((1.f - ld) - loudDuck) * (ld > 1.f - loudDuck ? 0.6f : 0.05f);
        fpDuckEnv *= expf(-blockSec / 0.2f);
        fpDuck = 1.f - 0.3f * fpDuckEnv;
        // ---- master sum
        float pg = pauseGain;
        float invN = 1.f / (float)n;
        for (int i = 0; i < n; i++) {
            float u = (float)i * invN;
            float spd = speechDuck0 + (speechDuck - speechDuck0) * u;
            float lsd = loudDuck0 + (loudDuck - loudDuck0) * u;
            float fpd = fpDuck0 + (fpDuck - fpDuck0) * u;
            float ambG = vSfx * pg * (0.45f + 0.55f * lsd) * fpd;
            float crowdG = ambG * (0.65f + 0.35f * spd);  // murmur yields a little to dialogue
            float musG = vMusic * spd * (0.7f + 0.3f * lsd);
            float wg = vSfx * pg * fpd;
            float l = wL[i] * wg + fpL[i] * vSfx * pg + wmL[i] * vMusic * pg * spd + aL[i] * ambG + cL[i] * crowdG +
                      rL[i] * vSfx * pg + uL[i] * vSfx + vL[i] * vVoice * pg + mL[i] * musG;
            float r = wR[i] * wg + fpR[i] * vSfx * pg + wmR[i] * vMusic * pg * spd + aR[i] * ambG + cR[i] * crowdG +
                      rR[i] * vSfx * pg + uR[i] * vSfx + vR[i] * vVoice * pg + mR[i] * musG;
            l = masterDcL.process(l * vMaster);
            r = masterDcR.process(r * vMaster);
            if (!std::isfinite(l)) l = 0.f;
            if (!std::isfinite(r)) r = 0.f;
            out[i * 2] = l;
            out[i * 2 + 1] = r;
        }
        // limiter (operates on de-interleaved temp to reuse the stereo processor)
        for (int i = 0; i < n; i++) {
            float l = out[i * 2], r = out[i * 2 + 1];
            limiter.processSample(l, r);
            out[i * 2] = l;
            out[i * 2 + 1] = r;
        }
        g_radioFrames.fetch_add(n, std::memory_order_relaxed);
        blockCounter++;
    }

    void renderVoices(int n, bool worldActive, float blockSec) {
        (void)blockSec;
        // virtualization: rank world voices by priority-weighted audibility
        int idx[kMaxVoices];
        int cnt = 0;
        for (int i = 0; i < kMaxVoices; i++) {
            Voice& v = voices[i];
            if (!v.active) continue;
            if (v.is3D) {
                computeSpatial(lis, env, v.pos3, vec3(), v.refDist, v.maxDist, v.airAbs, v.reverb, v.occl, v.erK, v.echoK, v.par);
                v.score = v.delay >= n ? -1.f : v.par.audibility * v.volume * (0.4f + (float)v.priority / 255.f);
            } else {
                v.score = 10.f + (float)v.priority;
            }
            idx[cnt++] = i;
        }
        if (cnt > kMaxRealVoices) {
            std::nth_element(idx, idx + kMaxRealVoices, idx + cnt, [&](int a, int b) { return voices[a].score > voices[b].score; });
            for (int k = 0; k < cnt; k++) voices[idx[k]].real = k < kMaxRealVoices;
        } else {
            for (int k = 0; k < cnt; k++) voices[idx[k]].real = true;
        }
        SendBus sb{sRev, sEr, sEcho};
        for (int k = 0; k < cnt; k++) {
            Voice& v = voices[idx[k]];
            bool worldVoice = v.bus != Bus::Ui;
            if (worldVoice && !worldActive) continue;  // frozen while paused
            // still travelling at the speed of sound
            int off = 0;
            if (v.delay > 0) {
                if (v.delay >= n) {
                    v.delay -= n;
                    continue;
                }
                off = v.delay;
                v.delay = 0;
            }
            const int m = n - off;
            float rate = v.rate * ((v.bus == Bus::Ui) ? 1.f : slowmo);
            if (v.is3D) rate *= v.par.doppler;
            if (v.is3D && v.score < kInaudible * 0.5f) v.real = false;
            bool finished = false;
            if (!v.real) {
                // advance virtually
                double len = v.speech ? (double)v.speech->pcm.size() : (double)v.buf->frames;
                v.pos += (double)rate * (double)m;
                if (v.pos >= len - 1.0 || v.stopping) finished = true;
                v.wasReal = false;
                v.sp.init = false;
            } else {
                finished = readVoice(v, rate, m);
                float g = v.volume;
                bool fadeIn = !v.wasReal && v.pos > (double)m * 1.5 + (double)rate * (double)m;
                if (v.fp && !v.wasReal) fpDuckEnv = 1.f;
                if (v.is3D) {
                    float* oL = (v.fp ? fpL : v.bus == Bus::Voice ? vL : wL) + off;
                    float* oR = (v.fp ? fpR : v.bus == Bus::Voice ? vR : wR) + off;
                    if (v.buf && v.buf->channels == 2) {
                        for (int i = 0; i < m; i++) mono[i] = 0.5f * (mono[i] + mono2[i]);
                    }
                    SendBus vsb{sb.rev + off, sb.er + off, sb.echo + off};
                    spatialize(v.sp, v.par, mono, oL, oR, vsb, m, g, fadeIn);
                } else {
                    float* oL = (v.fp ? fpL : v.bus == Bus::Ui ? uL : (v.bus == Bus::Voice ? vL : wL)) + off;
                    float* oR = (v.fp ? fpR : v.bus == Bus::Ui ? uR : (v.bus == Bus::Voice ? vR : wR)) + off;
                    bool st2 = v.buf && v.buf->channels == 2;
                    float fin = fadeIn ? 0.f : 1.f;
                    float pl = 1.f, pr = 1.f;
                    if (v.pan2D != 0.f) {
                        panGains(v.pan2D, pl, pr);
                        pl *= 1.41421f;
                        pr *= 1.41421f;
                    }
                    const float* srcL = (st2 && v.flip) ? mono2 : mono;
                    const float* srcR = st2 ? (v.flip ? mono : mono2) : mono;
                    for (int i = 0; i < m; i++) {
                        float f = fin + (1.f - fin) * ((float)i / (float)m);
                        oL[i] += srcL[i] * g * f * pl;
                        oR[i] += srcR[i] * g * f * pr;
                    }
                    if (v.bus == Bus::World) {
                        // a sound at the listener: sends as a 3D source at 1 m
                        float es = env.sendScale;
                        float rs = g * v.reverb * 0.45f * es, ers = g * v.reverb * v.erK, ecs = g * v.reverb * v.echoK;
                        float* r0 = sb.rev + off;
                        float* e0 = sb.er + off;
                        float* c0 = sb.echo + off;
                        for (int i = 0; i < m; i++) {
                            float x = 0.5f * (mono[i] + (st2 ? mono2[i] : mono[i]));
                            r0[i] += x * rs;
                            e0[i] += x * ers;
                            c0[i] += x * ecs;
                        }
                    }
                }
                v.wasReal = true;
            }
            if (finished) freeVoice(v, false);
        }
    }

    // Reads n resampled frames from the voice source into mono (and mono2 for stereo buffers).
    bool readVoice(Voice& v, float rate, int n) {
        bool stereo = v.buf && v.buf->channels == 2;
        const i16* d16 = v.buf ? v.buf->data.data() : nullptr;
        const float* df = v.speech ? v.speech->pcm.data() : nullptr;
        int len = v.speech ? (int)v.speech->pcm.size() : v.buf->frames;
        const float k16 = 1.f / 32767.f;
        bool finished = false;
        float stopK = 1.f;
        if (v.stopping) stopK = expf(-1.f / (0.006f * kSR));
        for (int i = 0; i < n; i++) {
            int ip = (int)v.pos;
            if (ip + 2 >= len) {
                for (int k = i; k < n; k++) mono[k] = mono2[k] = 0.f;
                finished = true;
                break;
            }
            float t = (float)(v.pos - (double)ip);
            float a, b, c, d;
            int im1 = ip > 0 ? ip - 1 : 0;
            if (df) {
                a = df[im1]; b = df[ip]; c = df[ip + 1]; d = df[ip + 2];
                mono[i] = hermite(a, b, c, d, t);
                mono2[i] = mono[i];
            } else if (!stereo) {
                a = (float)d16[im1] * k16; b = (float)d16[ip] * k16; c = (float)d16[ip + 1] * k16; d = (float)d16[ip + 2] * k16;
                mono[i] = hermite(a, b, c, d, t);
            } else {
                a = (float)d16[im1 * 2] * k16; b = (float)d16[ip * 2] * k16; c = (float)d16[(ip + 1) * 2] * k16; d = (float)d16[(ip + 2) * 2] * k16;
                mono[i] = hermite(a, b, c, d, t);
                a = (float)d16[im1 * 2 + 1] * k16; b = (float)d16[ip * 2 + 1] * k16; c = (float)d16[(ip + 1) * 2 + 1] * k16; d = (float)d16[(ip + 2) * 2 + 1] * k16;
                mono2[i] = hermite(a, b, c, d, t);
            }
            if (v.stopping) {
                v.stopGain *= stopK;
                mono[i] *= v.stopGain;
                mono2[i] *= v.stopGain;
            }
            v.pos += (double)rate;
        }
        if (v.stopping && v.stopGain < 1e-3f) finished = true;
        return finished;
    }

    void renderEmitters(int n, float blockSec) {
        static int idx[kMaxEmitters];
        int cnt = 0;
        const float ok = 1.f - expf(-blockSec / 0.15f);
        for (int i = 0; i < kMaxEmitters; i++) {
            Emitter& e = emitters[i];
            if (!e.active) continue;
            const EmitterDef& d = emitterDef(e.type);
            float occT = g_emOcclHandle[i].load(std::memory_order_acquire) == e.handle ? g_emOccl[i].load(std::memory_order_relaxed)
                                                                                       : acoustics::probeOcclusion(st.acoustic, e.pos - lis.pos);
            e.occl += (occT - e.occl) * ok;
            computeSpatial(lis, env, e.pos, e.vel, d.refDist, d.maxDist, 1.f, d.reverb, e.occl, 0.6f, 0.f, e.par);
            e.score = e.par.audibility * Max(e.volume, 0.05f) * (0.4f + (float)d.priority / 255.f);
            if (e.releasing) e.score *= 0.5f;
            idx[cnt++] = i;
        }
        int realMax = kMaxRealEmitters;
        if (cnt > realMax) {
            std::nth_element(idx, idx + realMax, idx + cnt, [&](int a, int b) { return emitters[a].score > emitters[b].score; });
            for (int k = 0; k < cnt; k++) emitters[idx[k]].real = k < realMax && emitters[idx[k]].score > kInaudible;
        } else {
            for (int k = 0; k < cnt; k++) emitters[idx[k]].real = emitters[idx[k]].score > kInaudible;
        }
        SendBus sb{sRev, sEr, sEcho};
        for (int k = 0; k < cnt; k++) {
            int slot = idx[k];
            Emitter& e = emitters[slot];
            const EmitterDef& d = emitterDef(e.type);
            if (!e.real) {
                if (e.releasing) {
                    freeEmitter(e, slot, false);
                    continue;
                }
                e.wasReal = false;
                e.sp.init = false;
                e.fifoLen = 2;
                e.fifo[0] = e.fifo[1] = 0.f;
                e.fifoPos = 1.0;
                continue;
            }
            bool fadeIn = !e.wasReal;
            float target = e.releasing ? 0.f : Max(e.volume, 0.f);
            if (e.type == EMIT_RADIO_WORLD) {
                int s = slotFor(Clamp((int)(e.p[0] + 0.5f), 0, stationCount() - 1));
                for (int i = 0; i < n; i++) {
                    float m = s >= 0 ? 0.5f * (slotL[s][i] + slotR[s][i]) : 0.f;
                    mono[i] = fastTanh(e.spkLp.process(e.spkHp.process(m)) * 1.4f) * 0.8f;
                }
            } else if (e.synth) {
                bool cabin = false;
                if (e.vehicle) {
                    vec3 toL = lis.pos - e.pos;
                    float dl = length(toL);
                    cabin = e.player && env.inVehicle > 0.5f && dl < 4.f;
                    e.synth->setListener(dl > 0.3f ? dot(toL / dl, e.fwd) : 0.f, cabin);
                }
                float aud = e.par.audibility * Max(e.volume, 0.f);
                e.synth->setLod(cabin ? 0 : aud < 0.025f ? 2 : aud < 0.08f ? 1 : 0);
                float rate = e.par.doppler * slowmo;
                for (int i = 0; i < n; i++) {
                    if (e.fifoPos + 3.0 >= (double)e.fifoLen) {
                        int keep = (int)e.fifoPos - 1;
                        if (keep > 0) {
                            memmove(e.fifo, e.fifo + keep, sizeof(float) * (size_t)(e.fifoLen - keep));
                            e.fifoLen -= keep;
                            e.fifoPos -= (double)keep;
                        }
                        int gen = Min(64, 320 - e.fifoLen);
                        e.synth->render(e.fifo + e.fifoLen, gen);
                        e.fifoLen += gen;
                    }
                    int ip = (int)e.fifoPos;
                    float t = (float)(e.fifoPos - (double)ip);
                    mono[i] = e.dc.process(hermite(e.fifo[ip - 1], e.fifo[ip], e.fifo[ip + 1], e.fifo[ip + 2], t));
                    e.fifoPos += (double)rate;
                }
                int shot = emitterPollOneShot(e.type, e.synth);
                if (shot > 0) {
                    vec3 off(rng.range(-6.f, 6.f), rng.range(-6.f, 6.f), 0.f);
                    spawnOneShot(shot, e.pos + off, rng.range(0.25f, 0.5f) * Max(e.volume, 0.f), rng.range(0.92f, 1.08f));
                }
            } else {
                for (int i = 0; i < n; i++) mono[i] = 0.f;
            }
            // volume smoothing (attack/release) applied pre-spatialization
            float v0 = e.volSmooth;
            float coef = e.releasing ? 0.25f : 0.35f;
            e.volSmooth += (target - e.volSmooth) * coef;
            if (e.releasing && e.volSmooth < 1e-3f) e.volSmooth = 0.f;
            for (int i = 0; i < n; i++) mono[i] *= v0 + (e.volSmooth - v0) * ((float)i / (float)n);
            bool isMusic = e.type == EMIT_RADIO_WORLD;
            spatialize(e.sp, e.par, mono, isMusic ? wmL : wL, isMusic ? wmR : wR, sb, n, d.gain, fadeIn);
            // cabin-only sounds of the player's vehicle (indicator relay, cabin wind): 2D, no outside muffling
            if (e.vehicle && e.player && e.synth && env.inVehicle > 0.5f && length2(lis.pos - e.pos) < 16.f) {
                for (int i = 0; i < n; i++) mono2[i] = 0.f;
                if (e.synth->renderCabin(mono2, n))
                    for (int i = 0; i < n; i++) {
                        wL[i] += mono2[i];
                        wR[i] += mono2[i];
                    }
            }
            e.wasReal = true;
            if (e.releasing && e.volSmooth <= 0.f) freeEmitter(e, slot, false);
        }
    }

    void renderRadio(int n, float blockSec) {
        // desired stations: in-car first, then audible world radio emitters
        int desired[kStationSlots];
        int nd = 0;
        if (st.radioStation >= 0 && st.radioStation < stationCount()) desired[nd++] = st.radioStation;
        if (pauseGain > 0.f) {
            struct Cand { int station; float score; };
            Cand cands[16];
            int nc = 0;
            for (int i = 0; i < kMaxEmitters && nc < 16; i++) {
                Emitter& e = emitters[i];
                if (!e.active || e.type != EMIT_RADIO_WORLD || e.releasing) continue;
                const EmitterDef& d = emitterDef(e.type);
                vec3 rel = e.pos - lis.pos;
                float dist = length(rel);
                if (dist > d.maxDist) continue;
                int s = Clamp((int)(e.p[0] + 0.5f), 0, stationCount() - 1);
                float sc = 1.f / (1.f + dist);
                bool found = false;
                for (int c = 0; c < nc; c++)
                    if (cands[c].station == s) { cands[c].score = Max(cands[c].score, sc); found = true; }
                if (!found) cands[nc++] = Cand{s, sc};
            }
            std::sort(cands, cands + nc, [](const Cand& a, const Cand& b) { return a.score > b.score; });
            for (int c = 0; c < nc && nd < kStationSlots; c++) {
                bool dup = false;
                for (int k = 0; k < nd; k++) dup |= desired[k] == cands[c].station;
                if (!dup) desired[nd++] = cands[c].station;
            }
        }
        assignSlots(desired, nd, blockSec);
        for (int s = 0; s < kStationSlots; s++) {
            if (slotStation[s] >= 0) slotGot[s] = readSlot(s, slotL[s], slotR[s], n, slotGen[s], true);
            else {
                slotGot[s] = 0;
                for (int i = 0; i < n; i++) slotL[s][i] = slotR[s][i] = 0.f;
            }
        }
        // in-car radio
        int want = st.radioStation >= 0 && st.radioStation < stationCount() ? st.radioStation : -1;
        if (want != radioStation) {
            radioStation = want;
            staticTime = 0.f;
            staticMin = want >= 0 ? 0.32f : 0.12f;
        }
        int s = want >= 0 ? slotFor(want) : -1;
        bool have = s >= 0 && slotGot[s] >= n;
        float staticLevel = 0.f;
        if (staticMin > 0.f) {
            staticTime += blockSec;
            bool waiting = want >= 0 && !have && staticTime < 1.5f;
            if (staticTime < staticMin || waiting) staticLevel = 1.f;
            else staticMin = 0.f;
        }
        float targetGain = (want >= 0 && have && staticLevel <= 0.f) ? 1.f : 0.f;
        float ri = Saturate(st.radioInterior);
        radioInt += (ri - radioInt) * 0.2f;
        float cut = Lerp(900.f, 12000.f, radioInt);
        if (fabsf(cut - radioCut) > 20.f || blockCounter == 0) {
            radioCut = cut;
            float g = svfG(cut);
            carLpL.setG(g, 0.707f);
            carLpR.setG(g, 0.707f);
        }
        float lvl = Lerp(0.3f, 1.f, radioInt);
        float g0 = radioGain;
        for (int i = 0; i < n; i++) {
            radioGain += (targetGain - radioGain) * (targetGain > radioGain ? 0.004f : 0.02f);
            float l = 0.f, r = 0.f;
            if (s >= 0 && radioGain > 1e-4f) {
                l = slotL[s][i] * radioGain;
                r = slotR[s][i] * radioGain;
            }
            if (staticLevel > 0.f || staticWhistle > 1e-4f) {
                staticWhistle += (staticLevel - staticWhistle) * 0.004f;
                if ((i & 15) == 0) staticBp.setG(svfG(1200.f + 2500.f * nz.uni()), 0.9f);
                if (nz.uni() < 0.004f) staticCrackle = nz.range(0.3f, 1.f);
                staticCrackle *= 0.97f;
                float w = nz.white();
                float stc = (staticBp.bp(w) * 0.5f + w * 0.12f + nz.white() * staticCrackle * 0.6f) * staticWhistle * 0.22f;
                l += stc;
                r += stc;
            }
            l = carBoomL.process(carHpL.process(l));
            r = carBoomR.process(carHpR.process(r));
            if (radioInt < 0.98f) {
                l = carLpL.lp(l);
                r = carLpR.lp(r);
            }
            mL[i] += l * lvl * 0.9f;
            mR[i] += r * lvl * 0.9f;
        }
        (void)g0;
    }

    void renderScore(int n, float blockSec) {
        ProducerSlot& ps = producerSlot(kScoreSlot);
        ps.wantedStation.store(st.scoreMood, std::memory_order_release);
        ps.intensity.store(Saturate(st.scoreIntensity), std::memory_order_release);
        int got = readSlot(kScoreSlot, slotL[kScoreSlot], slotR[kScoreSlot], n, 0, false);
        (void)blockSec;
        (void)got;
        for (int i = 0; i < n; i++) {
            mL[i] += slotL[kScoreSlot][i] * 0.85f;
            mR[i] += slotR[kScoreSlot][i] * 0.85f;
        }
    }

    void spawnOneShot(int id, vec3 pos, float vol, float pitch) {
        u32 h = g_nextHandle.fetch_add(1);
        if (h == 0) h = g_nextHandle.fetch_add(1);
        g_handleState[h & (kHandleTable - 1)].store(h);
        startSfx(id, h, false, pos, vol, pitch);
    }
    void spawn2D(int id, float vol, float pitch, bool flip) {
        if (id <= 0 || id >= BANK_COUNT || !bankReady(id)) return;
        const SoundDef& d = soundDef(id);
        BankEntry& e = bankEntry(id);
        if (e.count <= 0) return;
        Voice* v = allocVoice(d.priority);
        if (!v) return;
        *v = Voice();
        v->active = true;
        v->id = id;
        v->buf = &e.vars[rng.irange(0, e.count - 1)];
        v->rate = pitch * (1.f + rng.range(-d.pitchVar, d.pitchVar)) / (float)v->buf->rateDiv;
        v->volume = vol * d.gain;
        v->bus = Bus::World;
        v->is3D = false;
        v->reverb = d.reverb;
        v->priority = d.priority;
        v->echoK = echoWeight(id);
        v->flip = flip;
        v->sp.reset();
    }
};

Mixer* g_mixer = nullptr;
std::mutex g_renderMutex;
std::atomic<bool> g_inited{false};
std::atomic<bool> g_deviceOk{false};
std::atomic<bool> g_offline{false};  // renderOffline() in use (tests / capture without a device)
bool g_async = false;
std::mutex g_lifeMutex;

// Sound-producing API calls are live with an output device or once renderOffline() drives the mixer;
// otherwise (init() found no device) they are no-ops returning invalid handles.
static inline bool apiLive() { return g_inited.load() && (g_deviceOk.load() || g_offline.load()); }

// Acoustic probe (game thread: update() casts a few rays per frame through the game's raycast).
std::atomic<RaycastFn> g_raycast{nullptr};
acoustics::Probe g_probe;
constexpr int kProbeRaysPerFrame = 4;

// Occlusion rays (game thread). Emitters are re-tested round-robin; one-shots get one ray when played from the
// update thread. Results reach the mixer through per-slot atomics; without a ray the mixer uses the probe estimate.
struct EmitterView {
    u32 handle = 0;
    vec3 pos;
    float maxDist = 0.f;
};
EmitterView g_emView[kMaxEmitters];                   // guarded by g_cmdMutex
int g_occlCursor = 0;
std::thread::id g_updateThread;
vec3 g_listenerPos;                                   // update thread only
int g_oneShotRays = 0;                                // update thread only
constexpr int kEmitterRaysPerFrame = 6, kOneShotRaysPerFrame = 8;

// 0 clear line of sight, ~0.55 blocked but heard over the obstacle, 1 blocked.
static float occlusionRays(RaycastFn fn, vec3 from, vec3 to) {
    vec3 d = to - from;
    float len = length(d);
    if (len < 4.f || !std::isfinite(len)) return 0.f;
    float hit;
    if (!fn(from, d / len, len - 1.f, &hit)) return 0.f;
    vec3 over = to + vec3(0.f, 0.f, 8.f + 0.04f * len);   // diffraction over roofs / walls
    vec3 d2 = over - from;
    float l2 = length(d2);
    if (!fn(from, d2 / l2, l2 - 1.f, &hit)) return 0.55f;
    return 1.f;
}

// Occlusion of a one-shot at play time (-1: unknown, the mixer falls back to the probe estimate).
static float oneShotOcclusion(vec3 pos, float maxDist) {
    RaycastFn fn = g_raycast.load();
    if (!fn || g_oneShotRays <= 0 || std::this_thread::get_id() != g_updateThread) return -1.f;
    float d = length(pos - g_listenerPos);
    if (d < 4.f || d > maxDist) return -1.f;
    g_oneShotRays--;
    return occlusionRays(fn, g_listenerPos, pos + vec3(0.f, 0.f, 0.5f));
}

static void renderCallback(float* out, int frames) {
    static thread_local bool s_ftz = false;
    if (!s_ftz) {
        enableFlushDenormals();
        s_ftz = true;
    }
    if (!g_renderMutex.try_lock()) {
        memset(out, 0, sizeof(float) * 2 * (size_t)frames);
        return;
    }
    int done = 0;
    while (done < frames) {
        int n = Min(kMaxBlock, frames - done);
        g_mixer->render(out + done * 2, n);
        done += n;
    }
    g_renderMutex.unlock();
}

static void initCore() {
    for (auto& h : g_handleState) h.store(0);
    for (auto& b : g_emitterBusy) b.store(0);
    memset(g_emitterGen, 0, sizeof(g_emitterGen));
    g_pending.reserve(4096);
    g_processing.reserve(4096);
    g_mixer = new Mixer();
    g_mixer->init();
    u64 session = (u64)std::chrono::high_resolution_clock::now().time_since_epoch().count();
    musicInit(session ^ 0x9E3779B97F4A7C15ULL);
}

static bool pushCmd(const Cmd& c) {
    std::lock_guard<std::mutex> lk(g_cmdMutex);
    if (g_pending.size() >= kMaxPendingCmds) return false;
    g_pending.push_back(c);
    return true;
}

static u32 newVoiceHandle() {
    u32 h = g_nextHandle.fetch_add(1);
    if (h == 0) h = g_nextHandle.fetch_add(1);
    return h;
}

}  // namespace mix

void spawnWorldOneShot(int bankId, vec3 worldPos, float volume, float pitch) {
    if (mix::g_mixer) mix::g_mixer->spawnOneShot(bankId, worldPos, volume, pitch);
}
void spawnAmbient2D(int bankId, float volume, float pitch, bool flip) {
    if (mix::g_mixer) mix::g_mixer->spawn2D(bankId, volume, pitch, flip);
}

void dialogSpeechDropped(SpeechJob* j) {
    if (j) mix::handleClear(j->handle);
}

void dialogSpeechReady(SpeechJob* j) {
    j->refs.fetch_add(1);
    mix::Cmd c = {};
    c.type = mix::CmdType::SpeechReady;
    c.job = j;
    c.handle = j->handle;
    if (!mix::pushCmd(c)) {
        mix::handleClear(j->handle);
        speechJobRelease(j);
    }
}

}  // namespace detail

// =============================================================================================
// Public API
using namespace detail;

bool init() {
    std::lock_guard<std::mutex> lk(mix::g_lifeMutex);
    if (mix::g_inited.load()) {
        // Calling init() again after it found no device retries opening one (unless the mixer is
        // already being driven by renderOffline()).
        if (mix::g_deviceOk.load() || mix::g_offline.load()) return mix::g_deviceOk.load();
    } else {
        mix::initCore();
        mix::g_inited.store(true);
    }
    bool ok = backend::start(&mix::renderCallback);
    if (ok) {
        mix::g_async = true;
        speechStart(true);
        musicStartThread();
        int hc = (int)std::thread::hardware_concurrency();
        bankStartAsync(Clamp(hc - 1, 1, 4));
        crowdStart(true);
        mix::g_deviceOk.store(true);
        LOG("Audio: output started (procedural bank rendering in background)");
    } else {
        mix::g_async = false;
        speechStart(false);
        LOG("Audio: no output device - audio disabled");
    }
    return ok;
}

void shutdown() {
    std::lock_guard<std::mutex> lk(mix::g_lifeMutex);
    if (!mix::g_inited.load()) return;
    backend::stop();
    musicShutdown();
    speechStop();
    bankShutdown();
    crowdStop();
    {
        std::lock_guard<std::mutex> rl(mix::g_renderMutex);
        mix::g_mixer->destroy();
        delete mix::g_mixer;
        mix::g_mixer = nullptr;
    }
    {
        std::lock_guard<std::mutex> cl(mix::g_cmdMutex);
        for (auto& c : mix::g_pending)
            if (c.type == mix::CmdType::SpeechReady && c.job) speechJobRelease(c.job);
        mix::g_pending.clear();
    }
    mix::garbageDrain();
    mix::g_inited.store(false);
    mix::g_deviceOk.store(false);
    mix::g_offline.store(false);
    mix::g_async = false;
}

void update(const Listener& listener, float dt) {
    (void)dt;
    if (!mix::g_inited.load()) return;
    // environment: probe the geometry around the listener (reverb zones, early reflections, occlusion)
    float urban;
    {
        std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
        urban = mix::g_shared.amb.urban;
    }
    AcousticState ac;
    float interiorHint = Saturate(listener.interior) * (1.f - Saturate(listener.inVehicle));
    RaycastFn fn = mix::g_raycast.load();
    mix::g_updateThread = std::this_thread::get_id();
    mix::g_listenerPos = listener.pos;
    mix::g_oneShotRays = mix::kOneShotRaysPerFrame;
    if (fn) {
        mix::g_probe.step(listener.pos, fn, mix::kProbeRaysPerFrame);
        mix::g_probe.fill(ac);
        acoustics::analyze(ac, interiorHint, urban);
        // occlusion of emitters behind buildings / walls, a few per frame
        struct Job {
            int slot;
            u32 handle;
            vec3 pos;
        };
        Job jobs[mix::kEmitterRaysPerFrame];
        int nj = 0;
        {
            std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
            for (int k = 0; k < mix::kMaxEmitters && nj < mix::kEmitterRaysPerFrame; k++) {
                int slot = mix::g_occlCursor;
                mix::g_occlCursor = (mix::g_occlCursor + 1) % mix::kMaxEmitters;
                const mix::EmitterView& ev = mix::g_emView[slot];
                if (!ev.handle) continue;
                float d = length(ev.pos - listener.pos);
                if (d < 4.f || d > ev.maxDist) {
                    mix::g_emOccl[slot].store(0.f, std::memory_order_relaxed);
                    mix::g_emOcclHandle[slot].store(ev.handle, std::memory_order_release);
                    continue;
                }
                jobs[nj++] = {slot, ev.handle, ev.pos};
            }
        }
        for (int j = 0; j < nj; j++) {
            float o = mix::occlusionRays(fn, listener.pos, jobs[j].pos + vec3(0.f, 0.f, 0.6f));
            mix::g_emOccl[jobs[j].slot].store(o, std::memory_order_relaxed);
            mix::g_emOcclHandle[jobs[j].slot].store(jobs[j].handle, std::memory_order_release);
        }
    } else {
        acoustics::fallback(ac, interiorHint, urban);
    }
    {
        std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
        mix::g_shared.acoustic = ac;
        ListenerState& l = mix::g_shared.listener;
        l.pos = listener.pos;
        l.vel = listener.vel;
        l.forward = listener.forward;
        l.up = listener.up;
        l.interior = listener.interior;
        l.inVehicle = listener.inVehicle;
        l.bodySpeed = listener.bodySpeed;
        mix::g_shared.listenerSet = true;
    }
    mix::garbageDrain();
}

void setAmbience(const Ambience& a) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.amb = a;
}
void setCrowd(float density, int placeType, float panic) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.crowdDensity = std::isfinite(density) ? Saturate(density) : 0.f;
    mix::g_shared.crowdPlace = Clamp(placeType, 0, (int)CROWD_PLACE_COUNT - 1);
    mix::g_shared.crowdPanic = std::isfinite(panic) ? Saturate(panic) : 0.f;
}
void setPaused(bool paused) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.paused = paused;
}
void setSlowMotion(float factor) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.slowmo = Clamp(factor, 0.05f, 2.f);
}
void setMasterVolume(float v) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.master = Saturate(v);
}
void setSfxVolume(float v) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.sfx = Saturate(v);
}
void setMusicVolume(float v) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.music = Saturate(v);
}
void setVoiceVolume(float v) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.voice = Saturate(v);
}
void setDialogueDucking(float amount) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.duck = Clamp(amount * 1.24f, 0.f, 0.95f);
}

static SoundHandle playImpl(Sfx id, vec3 pos, float volume, float pitch, bool is2D) {
    if (!mix::apiLive() || (int)id <= 0 || (int)id >= SFX_COUNT) return 0;
    u32 h = mix::newVoiceHandle();
    mix::g_handleState[h & (mix::kHandleTable - 1)].store(h, std::memory_order_release);
    mix::Cmd c = {};
    c.type = mix::CmdType::Play;
    c.is2D = is2D;
    c.id = (i32)id;
    c.handle = h;
    c.pos = pos;
    c.volume = Max(volume, 0.f);
    c.pitch = Clamp(pitch, 0.1f, 4.f);
    c.p[1] = -1.f;
    if (!is2D) {
        const SoundDef& d = soundDef((int)id);
        if (d.bus == Bus::World && d.priority >= 90) c.p[1] = mix::oneShotOcclusion(pos, Min(d.maxDist, 400.f));
    }
    if (!mix::pushCmd(c)) {
        mix::handleClear(h);
        return 0;
    }
    return h;
}
SoundHandle play(Sfx id, vec3 pos, float volume, float pitch) { return playImpl(id, pos, volume, pitch, false); }
SoundHandle play2D(Sfx id, float volume, float pitch) { return playImpl(id, vec3(), volume, pitch, true); }

SoundHandle playGunshot(Sfx weapon, vec3 muzzle, vec3 dir, u32 flags, float volume, float pitch) {
    if (!mix::apiLive()) return 0;
    if (mix::Mixer::gunClassOf((int)weapon) < 0) return play(weapon, muzzle, volume, pitch);
    u32 h = mix::newVoiceHandle();
    mix::g_handleState[h & (mix::kHandleTable - 1)].store(h, std::memory_order_release);
    mix::Cmd c = {};
    c.type = mix::CmdType::Gunshot;
    c.id = (i32)weapon;
    c.handle = h;
    c.pos = muzzle;
    c.vel = std::isfinite(dir.x + dir.y + dir.z) ? dir : vec3();
    c.p[0] = (float)(flags & 0xffu);
    c.p[1] = (flags & GUN_PLAYER) ? 0.f : mix::oneShotOcclusion(muzzle, 900.f);
    c.volume = Max(volume, 0.f);
    c.pitch = Clamp(pitch, 0.1f, 4.f);
    if (!mix::pushCmd(c)) {
        mix::handleClear(h);
        return 0;
    }
    return h;
}

void setRaycast(RaycastFn fn) { mix::g_raycast.store(fn); }

void stop(SoundHandle h) {
    if (!mix::apiLive() || !h) return;
    mix::handleClear(h);
    mix::Cmd c = {};
    c.type = mix::CmdType::Stop;
    c.handle = h;
    mix::pushCmd(c);
}
bool isPlaying(SoundHandle h) {
    if (!mix::apiLive() || !h) return false;
    return mix::handleAlive(h);
}

EmitterHandle createEmitter(EmitterType type) {
    if (!mix::apiLive() || (int)type < 0 || (int)type >= EMIT_COUNT) return 0;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    static int s_cursor = 0;
    for (int k = 0; k < mix::kMaxEmitters; k++) {
        int slot = (s_cursor + k) % mix::kMaxEmitters;
        u8 expect = 0;
        if (!mix::g_emitterBusy[slot].compare_exchange_strong(expect, 1)) continue;
        s_cursor = slot + 1;
        u32 gen = ++mix::g_emitterGen[slot];
        u32 h = (gen * (u32)mix::kMaxEmitters) + (u32)slot;
        if (h == 0) h = (++mix::g_emitterGen[slot]) * (u32)mix::kMaxEmitters + (u32)slot;
        if (mix::g_pending.size() >= mix::kMaxPendingCmds) {
            mix::g_emitterBusy[slot].store(0);
            return 0;
        }
        mix::Cmd c = {};
        c.type = mix::CmdType::EmitterCreate;
        c.id = (i32)type;
        c.handle = h;
        c.volume = 1.f;
        c.pos = mix::g_shared.listener.pos + vec3(0.f, 0.f, -10000.f);  // silent until first setEmitter
        mix::g_pending.push_back(c);
        mix::EmitterView& ev = mix::g_emView[slot];
        ev.handle = 0;   // no occlusion rays until the first setEmitter places it
        ev.maxDist = emitterDef(type).maxDist;
        return h;
    }
    return 0;
}

void setEmitter(EmitterHandle h, vec3 pos, vec3 vel, float p0, float p1, float p2, float p3, float volume) {
    if (!mix::apiLive() || !h) return;
    mix::Cmd c = {};
    c.type = mix::CmdType::EmitterSet;
    c.handle = h;
    c.pos = pos;
    c.vel = vel;
    c.p[0] = p0;
    c.p[1] = p1;
    c.p[2] = p2;
    c.p[3] = p3;
    c.volume = Max(volume, 0.f);
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    if (mix::g_pending.size() >= mix::kMaxPendingCmds) return;
    mix::g_pending.push_back(c);
    mix::EmitterView& ev = mix::g_emView[h % (u32)mix::kMaxEmitters];
    if (std::isfinite(pos.x + pos.y + pos.z)) {
        ev.handle = h;
        ev.pos = pos;
    }
}

void setEngineTune(EmitterHandle h, float boost, float tune, bool shifted) {
    if (!mix::apiLive() || !h) return;
    mix::Cmd c = {};
    c.type = mix::CmdType::EmitterTune;
    c.handle = h;
    c.p[0] = Saturate(boost);
    c.p[1] = Saturate(tune);
    c.p[2] = shifted ? 1.f : 0.f;
    mix::pushCmd(c);
}

void setVehicleAudio(EmitterHandle h, const VehicleAudio& v) {
    if (!mix::apiLive() || !h) return;
    mix::Cmd c = {};
    c.type = mix::CmdType::EmitterVehicle;
    c.handle = h;
    c.va = v;
    if (!std::isfinite(c.va.speed + c.va.slip + c.va.lateralSlip + c.va.wetness + c.va.bump + c.va.damage)) return;
    mix::pushCmd(c);
}

void destroyEmitter(EmitterHandle h) {
    if (!mix::apiLive() || !h) return;
    mix::Cmd c = {};
    c.type = mix::CmdType::EmitterDestroy;
    c.handle = h;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::EmitterView& ev = mix::g_emView[h % (u32)mix::kMaxEmitters];
    if (ev.handle == h) ev.handle = 0;
    if (mix::g_pending.size() < mix::kMaxPendingCmds) mix::g_pending.push_back(c);
}

int radioStationCount() { return stationCount(); }
const char* radioStationName(int station) { return stationName(station); }
const char* radioStationGenre(int station) { return stationGenre(station); }
void setRadioStation(int station) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.radioStation = (station >= 0 && station < stationCount()) ? station : -1;
}
int radioStation() {
    if (!mix::g_inited.load()) return -1;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    return mix::g_shared.radioStation;
}
std::string radioNowPlaying(int station) {
    if (!mix::g_inited.load()) return std::string();
    double t = (double)g_radioFrames.load() / (double)detail::kSampleRate;
    return stationNowPlaying(station, t);
}
void setRadioInterior(float amount) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.radioInterior = Saturate(amount);
}

static SoundHandle speakImpl(const char* text, const VoiceParams& voice, float volume, bool positional, vec3 pos) {
    if (!mix::apiLive() || !text || !*text) return 0;
    u32 h = mix::newVoiceHandle();
    mix::g_handleState[h & (mix::kHandleTable - 1)].store(h, std::memory_order_release);
    SpeechJob* j = new SpeechJob();
    j->text = text;
    j->voice = voice;
    j->sampleRate = detail::kSampleRate;
    j->priority = 0;
    j->handle = h;
    j->volume = Max(volume, 0.f);
    j->positional = positional;
    j->pos = pos;
    speechSubmit(j);
    speechJobRelease(j);
    return h;
}
SoundHandle speak(const char* text, const VoiceParams& voice, float volume) { return speakImpl(text, voice, volume, false, vec3()); }
SoundHandle speakAt(const char* text, const VoiceParams& voice, vec3 pos, float volume) { return speakImpl(text, voice, volume, true, pos); }
float estimateSpeechDuration(const char* text, const VoiceParams& voice) {
    if (!text) return 0.f;
    return Speech::estimateDuration(text, voice) * speechDurationScale();
}

void playThunder(float distance, float volume) {
    if (!mix::apiLive() || !std::isfinite(distance)) return;
    mix::Cmd c = {};
    c.type = mix::CmdType::Thunder;
    c.p[0] = Max(distance, 0.f);
    c.volume = Saturate(volume) * 1.2f;
    mix::pushCmd(c);
}

void setScore(int moodSeed, float intensity) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.scoreMood = intensity > 0.f ? Max(moodSeed, 0) : -1;
    mix::g_shared.scoreIntensity = Saturate(intensity);
}

void renderOffline(float* outStereo, int frames) {
    {
        std::lock_guard<std::mutex> lk(mix::g_lifeMutex);
        if (!mix::g_inited.load()) {
            mix::initCore();
            mix::g_inited.store(true);
            mix::g_deviceOk.store(false);
            mix::g_async = false;
            speechStart(false);
        }
        mix::g_offline.store(true);
    }
    bankWaitAll();
    dsp::ScopedFlushDenormals ftz;
    std::lock_guard<std::mutex> rl(mix::g_renderMutex);
    mix::g_mixer->crowdPlayer.offline = true;  // no worker: crowd beds render on first use
    int done = 0;
    while (done < frames) {
        int n = Min(kMaxBlock, frames - done);
        if (!speechAsync()) speechPumpSync();
        musicPumpSync(n);
        if (outStereo) mix::g_mixer->render(outStereo + (size_t)done * 2, n);
        else {
            float tmp[kMaxBlock * 2];
            mix::g_mixer->render(tmp, n);
        }
        done += n;
    }
    mix::garbageDrain();
}

}  // namespace Audio
