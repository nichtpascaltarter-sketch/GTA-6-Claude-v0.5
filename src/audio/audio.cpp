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
constexpr int kMaxRealEmitters = 24;
constexpr int kHandleTable = 4096;
constexpr int kItdLen = 64;
constexpr float kSpeedOfSound = 343.f;

// ---------------------------------------------------------------------------------------------
enum class CmdType : u8 { Play, Stop, EmitterCreate, EmitterSet, EmitterDestroy, SpeechReady };
struct Cmd {
    CmdType type;
    bool is2D;
    i32 id;
    u32 handle;
    vec3 pos, vel;
    float p[4];
    float volume, pitch;
    SpeechJob* job;
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
};

std::mutex g_cmdMutex;
std::vector<Cmd> g_pending, g_processing;
SharedState g_shared;
constexpr size_t kMaxPendingCmds = 16384;

std::atomic<u32> g_handleState[kHandleTable];
std::atomic<u32> g_nextHandle{1};
std::atomic<u8> g_emitterBusy[kMaxEmitters];
u32 g_emitterGen[kMaxEmitters];

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
    float send = 0.f;
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
    float gl = 0, gr = 0, send = 0;
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
};

static void computeSpatial(const ListenerState& L, const WorldEnv& env, vec3 pos, vec3 vel, float refDist, float maxDist,
                           float airAbs, float reverbAmt, SpatialParams& o) {
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
    // mono low-pass: rear shadow, air absorption, enclosure, vehicle, underwater, slow motion
    float fcRear = y < 0.f ? Lerp(20000.f, 7500.f, -y * closeK) : 20000.f;
    float fcAir = 22000.f / (1.f + d * airAbs / 60.f);
    float fcInt = Lerp(22000.f, 2600.f, env.interior * SmoothStep(8.f, 30.f, d));
    float fcVeh = Lerp(22000.f, 1100.f, env.inVehicle);
    float fcUw = Lerp(22000.f, 420.f, env.underwater);
    float fcSlow = Lerp(4500.f, 22000.f, SmoothStep(0.3f, 1.f, env.slowmo));
    o.cutoff = Min(Min(Min(fcRear, fcAir), Min(fcInt, fcVeh)), Min(fcUw, fcSlow));
    float occl = Lerp(1.f, 0.45f, env.inVehicle) * Lerp(1.f, 0.6f, env.underwater);
    o.gl *= occl;
    o.gr *= occl;
    // reverb send: grows relative to the direct sound with distance and enclosure
    float farK = 0.45f + 0.55f * SmoothStep(3.f, 80.f, d);
    o.send = reverbAmt * sqrtf(Max(g, 0.f)) * farK * (1.f + 1.5f * env.interior) * occl;
    // doppler
    float vl = dot(L.vel, dir), vs = dot(vel, dir);
    float dop = (kSpeedOfSound + vl) / Max(kSpeedOfSound + vs, 30.f);
    o.doppler = Clamp(dop, 0.5f, 2.f);
    o.audibility = g * occl;
}

// Processes a mono source block through the spatial state into L/R (+ reverb send).
static void spatialize(SpatialState& st, const SpatialParams& p, const float* in, float* outL, float* outR, float* sendL,
                       float* sendR, int n, float gain, bool fadeIn) {
    if (!st.init) {
        st.gl = p.gl * gain;
        st.gr = p.gr * gain;
        st.send = p.send * gain;
        st.itdL = p.itdL;
        st.itdR = p.itdR;
        st.cutoff = p.cutoff;
        st.lp.setG(svfG(p.cutoff), 0.707f);
        st.init = true;
        if (fadeIn) st.gl = st.gr = st.send = 0.f;
    }
    // smooth parameter targets across blocks (reduces zipper from game-rate position updates)
    float tgl = p.gl * gain, tgr = p.gr * gain, ts = p.send * gain;
    float gl0 = st.gl, gr0 = st.gr, s0 = st.send;
    float gl1 = gl0 + (tgl - gl0) * 0.5f, gr1 = gr0 + (tgr - gr0) * 0.5f, s1 = s0 + (ts - s0) * 0.5f;
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
        xl = st.farL.process(xl);
        xr = st.farR.process(xr);
        float gl = gl0 + (gl1 - gl0) * u, gr = gr0 + (gr1 - gr0) * u, sg = s0 + (s1 - s0) * u;
        outL[i] += xl * gl;
        outR[i] += xr * gr;
        float m = 0.5f * (xl + xr) * sg;
        sendL[i] += m;
        sendR[i] += m;
    }
    st.gl = gl1;
    st.gr = gr1;
    st.send = s1;
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
};

struct Mixer {
    Voice voices[kMaxVoices];
    Emitter* emitters = nullptr;
    SharedState st;
    ListenerState lis;
    WorldEnv env;
    AmbienceRenderer* amb = nullptr;
    FdnReverb reverb;
    LookaheadLimiter limiter;
    Noise nz{0x77u};
    float pauseGain = 1.f;
    float slowmo = 1.f;
    // smoothed bus volumes
    float vMaster = 1, vSfx = 1, vMusic = 1, vVoice = 1;
    // ducking
    EnvFollower speechEnv, loudEnv;
    float speechDuck = 1.f, loudDuck = 1.f;
    // reverb character (interior-driven)
    float revInterior = -1.f;
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
    float uL[kMaxBlock], uR[kMaxBlock];      // ui
    float vL[kMaxBlock], vR[kMaxBlock];      // dialogue
    float mL[kMaxBlock], mR[kMaxBlock];      // music (radio + score)
    float sL[kMaxBlock], sR[kMaxBlock];      // reverb send
    float rL[kMaxBlock], rR[kMaxBlock];      // reverb return
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
        reverb.init(1.25f, 0x5EEDu);
        reverb.setDecay(1.4f, 0.45f);
        reverb.setPreDelay(0.02f);
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
    void startSfx(int id, u32 handle, bool is2D, vec3 pos, float volume, float pitch) {
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
        v->rate = pitch * (1.f + rng.range(-d.pitchVar, d.pitchVar));
        v->volume = volume * d.gain;
        v->bus = d.bus;
        v->is3D = !is2D && d.bus == Bus::World;
        v->pos3 = pos;
        v->refDist = d.refDist;
        v->maxDist = d.maxDist;
        v->reverb = d.reverb;
        v->airAbs = d.airAbs;
        v->priority = d.priority;
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
                case CmdType::Play: startSfx(c.id, c.handle, c.is2D, c.pos, c.volume, c.pitch); break;
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
        // reverb character follows the enclosure
        float ri = env.interior * 0.8f + env.underwater * 0.2f;
        if (fabsf(ri - revInterior) > 0.02f) {
            revInterior = ri;
            reverb.setDecay(Lerp(1.3f, 2.4f, ri), Lerp(0.5f, 0.3f, ri) + env.underwater * 0.4f);
            reverb.erLevel = Lerp(0.25f, 0.6f, ri);
        }
        for (int i = 0; i < n; i++) {
            wL[i] = wR[i] = wmL[i] = wmR[i] = aL[i] = aR[i] = uL[i] = uR[i] = 0.f;
            vL[i] = vR[i] = mL[i] = mR[i] = sL[i] = sR[i] = 0.f;
        }
        bool worldActive = pauseGain > 0.f;
        // ---- ambience
        if (worldActive && amb) {
            radioSetContext(st.amb.timeOfDay, st.amb.rain);
            ambienceRender(amb, aL, aR, n, st.amb, lis);
            for (int i = 0; i < n; i++) {
                aL[i] = ambDcL.process(aL[i]);
                aR[i] = ambDcR.process(aR[i]);
            }
        }
        // ---- voices
        renderVoices(n, worldActive);
        // ---- radio producers (in-car + world radio sources)
        renderRadio(n, blockSec);
        // ---- emitters
        if (worldActive) renderEmitters(n);
        // ---- score
        renderScore(n, blockSec);
        // ---- reverb
        reverb.process(sL, sR, rL, rR, n);
        // ---- ducking envelopes
        float sEnvMax = 0.f, lEnvMax = 0.f;
        for (int i = 0; i < n; i++) {
            float v = Max(fabsf(vL[i]), fabsf(vR[i])) * vVoice;
            sEnvMax = Max(sEnvMax, speechEnv.process(v));
            float w = Max(fabsf(wL[i]), fabsf(wR[i])) * vSfx;
            lEnvMax = Max(lEnvMax, loudEnv.process(w));
        }
        float sd = 1.f - 0.62f * SmoothStep(0.004f, 0.05f, sEnvMax);
        float ld = SmoothStep(0.3f, 1.2f, lEnvMax);
        float speechDuck0 = speechDuck, loudDuck0 = loudDuck;
        speechDuck += (sd - speechDuck) * (sd < speechDuck ? 0.5f : 0.08f);
        loudDuck += ((1.f - ld) - loudDuck) * (ld > 1.f - loudDuck ? 0.6f : 0.05f);
        // ---- master sum
        float pg = pauseGain;
        float invN = 1.f / (float)n;
        for (int i = 0; i < n; i++) {
            float u = (float)i * invN;
            float spd = speechDuck0 + (speechDuck - speechDuck0) * u;
            float lsd = loudDuck0 + (loudDuck - loudDuck0) * u;
            float ambG = vSfx * pg * (0.45f + 0.55f * lsd);
            float musG = vMusic * spd * (0.7f + 0.3f * lsd);
            float l = wL[i] * vSfx * pg + wmL[i] * vMusic * pg * spd + aL[i] * ambG + rL[i] * vSfx * pg + uL[i] * vSfx + vL[i] * vVoice * pg +
                      mL[i] * musG;
            float r = wR[i] * vSfx * pg + wmR[i] * vMusic * pg * spd + aR[i] * ambG + rR[i] * vSfx * pg + uR[i] * vSfx + vR[i] * vVoice * pg +
                      mR[i] * musG;
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

    void renderVoices(int n, bool worldActive) {
        // virtualization: rank world voices by priority-weighted audibility
        int idx[kMaxVoices];
        int cnt = 0;
        for (int i = 0; i < kMaxVoices; i++) {
            Voice& v = voices[i];
            if (!v.active) continue;
            if (v.is3D) {
                computeSpatial(lis, env, v.pos3, vec3(), v.refDist, v.maxDist, v.airAbs, v.reverb, v.par);
                v.score = v.par.audibility * v.volume * (0.4f + (float)v.priority / 255.f);
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
        for (int k = 0; k < cnt; k++) {
            Voice& v = voices[idx[k]];
            bool worldVoice = v.bus != Bus::Ui;
            if (worldVoice && !worldActive) continue;  // frozen while paused
            float rate = v.rate * ((v.bus == Bus::Ui) ? 1.f : slowmo);
            if (v.is3D) rate *= v.par.doppler;
            if (v.is3D && v.par.audibility < 1e-5f) v.real = false;
            bool finished = false;
            if (!v.real) {
                // advance virtually
                double len = v.speech ? (double)v.speech->pcm.size() : (double)v.buf->frames;
                v.pos += (double)rate * (double)n;
                if (v.pos >= len - 1.0 || v.stopping) finished = true;
                v.wasReal = false;
                v.sp.init = false;
            } else {
                finished = readVoice(v, rate, n);
                float g = v.volume;
                bool fadeIn = !v.wasReal && v.pos > (double)n * 1.5;
                if (v.is3D) {
                    float* sendL = sL;
                    float* sendR = sR;
                    float* oL = v.bus == Bus::Voice ? vL : wL;
                    float* oR = v.bus == Bus::Voice ? vR : wR;
                    if (v.buf && v.buf->channels == 2) {
                        for (int i = 0; i < n; i++) mono[i] = 0.5f * (mono[i] + mono2[i]);
                    }
                    spatialize(v.sp, v.par, mono, oL, oR, sendL, sendR, n, g, fadeIn);
                } else {
                    float* oL = v.bus == Bus::Ui ? uL : (v.bus == Bus::Voice ? vL : wL);
                    float* oR = v.bus == Bus::Ui ? uR : (v.bus == Bus::Voice ? vR : wR);
                    bool st2 = v.buf && v.buf->channels == 2;
                    float fin = fadeIn ? 0.f : 1.f;
                    for (int i = 0; i < n; i++) {
                        float f = fin + (1.f - fin) * ((float)i / (float)n);
                        oL[i] += mono[i] * g * f;
                        oR[i] += (st2 ? mono2[i] : mono[i]) * g * f;
                    }
                    if (v.bus == Bus::World) {
                        for (int i = 0; i < n; i++) {
                            float m = 0.5f * (mono[i] + (st2 ? mono2[i] : mono[i])) * g * v.reverb * 0.5f;
                            sL[i] += m;
                            sR[i] += m;
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

    void renderEmitters(int n) {
        static int idx[kMaxEmitters];
        int cnt = 0;
        for (int i = 0; i < kMaxEmitters; i++) {
            Emitter& e = emitters[i];
            if (!e.active) continue;
            const EmitterDef& d = emitterDef(e.type);
            computeSpatial(lis, env, e.pos, e.vel, d.refDist, d.maxDist, 1.f, d.reverb, e.par);
            e.score = e.par.audibility * Max(e.volume, 0.05f) * (0.4f + (float)d.priority / 255.f);
            if (e.releasing) e.score *= 0.5f;
            idx[cnt++] = i;
        }
        int realMax = kMaxRealEmitters;
        if (cnt > realMax) {
            std::nth_element(idx, idx + realMax, idx + cnt, [&](int a, int b) { return emitters[a].score > emitters[b].score; });
            for (int k = 0; k < cnt; k++) emitters[idx[k]].real = k < realMax && emitters[idx[k]].score > 1e-5f;
        } else {
            for (int k = 0; k < cnt; k++) emitters[idx[k]].real = emitters[idx[k]].score > 1e-5f;
        }
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
                    mono[i] = hermite(e.fifo[ip - 1], e.fifo[ip], e.fifo[ip + 1], e.fifo[ip + 2], t);
                    e.fifoPos += (double)rate;
                }
                int shot = emitterPollOneShot(e.type, e.synth);
                if (shot > 0) {
                    vec3 off(rng.range(-6.f, 6.f), rng.range(-6.f, 6.f), 0.f);
                    spawnOneShot(shot, e.pos + off, rng.range(0.5f, 0.9f), rng.range(0.92f, 1.08f));
                }
            } else {
                for (int i = 0; i < n; i++) mono[i] = 0.f;
            }
            for (int i = 0; i < n; i++) mono[i] = e.dc.process(mono[i]);
            // volume smoothing (attack/release) applied pre-spatialization
            float v0 = e.volSmooth;
            float coef = e.releasing ? 0.25f : 0.35f;
            e.volSmooth += (target - e.volSmooth) * coef;
            if (e.releasing && e.volSmooth < 1e-3f) e.volSmooth = 0.f;
            for (int i = 0; i < n; i++) mono[i] *= v0 + (e.volSmooth - v0) * ((float)i / (float)n);
            bool isMusic = e.type == EMIT_RADIO_WORLD;
            spatialize(e.sp, e.par, mono, isMusic ? wmL : wL, isMusic ? wmR : wR, sL, sR, n, d.gain, fadeIn);
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
};

Mixer* g_mixer = nullptr;
std::mutex g_renderMutex;
std::atomic<bool> g_inited{false};
bool g_deviceOk = false;
bool g_async = false;
std::mutex g_lifeMutex;

static void renderCallback(float* out, int frames) {
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
    if (mix::g_inited.load()) return mix::g_deviceOk;
    mix::initCore();
    mix::g_inited.store(true);
    mix::g_deviceOk = backend::start(&mix::renderCallback);
    if (mix::g_deviceOk) {
        mix::g_async = true;
        speechStart(true);
        musicStartThread();
        int hc = (int)std::thread::hardware_concurrency();
        bankStartAsync(Clamp(hc - 1, 1, 4));
        LOG("Audio: output started (procedural bank rendering in background)");
    } else {
        mix::g_async = false;
        speechStart(false);
        LOG("Audio: no output device - audio disabled");
    }
    return mix::g_deviceOk;
}

void shutdown() {
    std::lock_guard<std::mutex> lk(mix::g_lifeMutex);
    if (!mix::g_inited.load()) return;
    backend::stop();
    musicShutdown();
    speechStop();
    bankShutdown();
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
    mix::g_deviceOk = false;
    mix::g_async = false;
}

void update(const Listener& listener, float dt) {
    (void)dt;
    if (!mix::g_inited.load()) return;
    {
        std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
        ListenerState& l = mix::g_shared.listener;
        l.pos = listener.pos;
        l.vel = listener.vel;
        l.forward = listener.forward;
        l.up = listener.up;
        l.interior = listener.interior;
        l.inVehicle = listener.inVehicle;
        mix::g_shared.listenerSet = true;
    }
    mix::garbageDrain();
}

void setAmbience(const Ambience& a) {
    if (!mix::g_inited.load()) return;
    std::lock_guard<std::mutex> lk(mix::g_cmdMutex);
    mix::g_shared.amb = a;
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

static SoundHandle playImpl(Sfx id, vec3 pos, float volume, float pitch, bool is2D) {
    if (!mix::g_inited.load() || (int)id <= 0 || (int)id >= SFX_COUNT) return 0;
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
    if (!mix::pushCmd(c)) {
        mix::handleClear(h);
        return 0;
    }
    return h;
}
SoundHandle play(Sfx id, vec3 pos, float volume, float pitch) { return playImpl(id, pos, volume, pitch, false); }
SoundHandle play2D(Sfx id, float volume, float pitch) { return playImpl(id, vec3(), volume, pitch, true); }

void stop(SoundHandle h) {
    if (!mix::g_inited.load() || !h) return;
    mix::handleClear(h);
    mix::Cmd c = {};
    c.type = mix::CmdType::Stop;
    c.handle = h;
    mix::pushCmd(c);
}
bool isPlaying(SoundHandle h) {
    if (!mix::g_inited.load() || !h) return false;
    return mix::handleAlive(h);
}

EmitterHandle createEmitter(EmitterType type) {
    if (!mix::g_inited.load() || (int)type < 0 || (int)type >= EMIT_COUNT) return 0;
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
        return h;
    }
    return 0;
}

void setEmitter(EmitterHandle h, vec3 pos, vec3 vel, float p0, float p1, float p2, float p3, float volume) {
    if (!mix::g_inited.load() || !h) return;
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
    mix::pushCmd(c);
}

void destroyEmitter(EmitterHandle h) {
    if (!mix::g_inited.load() || !h) return;
    mix::Cmd c = {};
    c.type = mix::CmdType::EmitterDestroy;
    c.handle = h;
    mix::pushCmd(c);
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
    if (!mix::g_inited.load() || !text || !*text) return 0;
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
            mix::g_deviceOk = false;
            mix::g_async = false;
            speechStart(false);
        }
    }
    bankWaitAll();
    std::lock_guard<std::mutex> rl(mix::g_renderMutex);
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
