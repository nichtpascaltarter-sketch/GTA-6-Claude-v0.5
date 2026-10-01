// Crowd walla beds: unintelligible multi-voice murmur loops (Speech::walla) per place type, rendered on a background
// thread at init, then looped by the mixer with a gain that follows the crowd density set by Audio::setCrowd().
// Two seeds per place play at once (panned apart, different loop lengths) so the bed never repeats audibly; place
// changes crossfade; outdoor crowds are muffled when the listener is indoors or in a closed car.
#include "audio_internal.h"
#include "speech_ext.h"

namespace Audio {
namespace detail {
namespace crowd {

constexpr int kSeeds = 2;
constexpr int kBedRate = 24000;  // babble is band-limited; played back at 48 kHz with interpolation

struct Bed {
    std::vector<float> pcm;
    std::atomic<bool> ready{false};
};
static Bed g_beds[CROWD_PLACE_COUNT][kSeeds];
static Bed g_panic[kSeeds];  // fleeing crowd (shared by all places)
static std::thread g_thread;
static std::atomic<bool> g_stop{false};
static std::mutex g_renderLock;  // serializes bed rendering (worker vs. offline on-demand)

static Speech::WallaParams bedParams(int place, int seed) {
    Speech::WallaParams p;
    p.seed = 0xC20Du + (u32)place * 977u + (u32)seed * 131u;
    p.seconds = seed == 0 ? 12.f : 14.5f;  // different loop lengths: the pair repeats only every ~2.5 minutes
    switch (place) {
        case CROWD_BEACH:
            p.voices = 9, p.femaleRatio = 0.55f, p.excitement = 0.6f, p.laughter = 0.14f;
            p.accent = Speech::ACCENT_LATINO, p.accentMix = 0.3f;
            break;
        case CROWD_CLUB:
            p.voices = 12, p.femaleRatio = 0.5f, p.excitement = 0.9f, p.laughter = 0.16f;
            p.accent = Speech::ACCENT_LATINO, p.accentMix = 0.25f;
            break;
        case CROWD_MALL:
            p.voices = 10, p.femaleRatio = 0.55f, p.excitement = 0.25f, p.laughter = 0.05f;
            break;
        default:  // CROWD_STREET
            p.voices = 8, p.femaleRatio = 0.5f, p.excitement = 0.35f, p.laughter = 0.06f;
            p.accent = Speech::ACCENT_LATINO, p.accentMix = 0.2f;
            break;
    }
    return p;
}

static void renderBed(int place, int seed) {
    std::lock_guard<std::mutex> lk(g_renderLock);
    Bed& b = g_beds[place][seed];
    if (b.ready.load(std::memory_order_acquire)) return;
    std::vector<float> pcm;
    Speech::walla(bedParams(place, seed), kBedRate, pcm);
    b.pcm.swap(pcm);
    b.ready.store(true, std::memory_order_release);
}

static void renderPanic(int seed) {
    std::lock_guard<std::mutex> lk(g_renderLock);
    Bed& b = g_panic[seed];
    if (b.ready.load(std::memory_order_acquire)) return;
    Speech::WallaParams p;
    p.seed = 0xFEA2u + (u32)seed * 313u;
    p.voices = 10, p.seconds = seed == 0 ? 8.f : 9.5f, p.excitement = 1.f, p.panic = 0.85f, p.laughter = 0.f;
    std::vector<float> pcm;
    Speech::walla(p, kBedRate, pcm);
    b.pcm.swap(pcm);
    b.ready.store(true, std::memory_order_release);
}

static void renderAll() {
    // one bed for every place first (street is the most common), then panic, then the second seeds
    static const int kOrder[CROWD_PLACE_COUNT] = {CROWD_STREET, CROWD_BEACH, CROWD_CLUB, CROWD_MALL};
    for (int s = 0; s < kSeeds; s++) {
        for (int p : kOrder) {
            if (g_stop.load()) return;
            renderBed(p, s);
        }
        if (g_stop.load()) return;
        renderPanic(s);
    }
}

// Mixer-owned playback state (audio thread).
struct Player {
    float gain[CROWD_PLACE_COUNT] = {0, 0, 0, 0};
    double pos[CROWD_PLACE_COUNT][kSeeds] = {};
    float panicGain = 0.f;
    double panicPos[kSeeds] = {};
    OnePoleLP mufL, mufR;
    float mufFc = -1.f;
    bool offline = false;

    // Adds a looped bed (linear interpolation, stereo placement) into L / R.
    static void playBed(const Bed& b, double& pos, float g, float gl, float gr, float* L, float* R, int n) {
        const float* x = b.pcm.data();
        const double len = (double)b.pcm.size(), step = (double)kBedRate / (double)kSR;
        double ps = pos;
        for (int i = 0; i < n; i++) {
            size_t i0 = (size_t)ps;
            float fr = (float)(ps - (double)i0);
            size_t i1 = i0 + 1 >= b.pcm.size() ? 0 : i0 + 1;
            float v = (x[i0] + (x[i1] - x[i0]) * fr) * g;
            L[i] += v * gl;
            R[i] += v * gr;
            ps += step;
            if (ps >= len) ps -= len;
        }
        pos = ps;
    }

    void render(float* L, float* R, int n, float density, int place, float panic, float interior, float inVehicle) {
        const float blockSec = (float)n * kInvSR;
        place = Clamp(place, 0, CROWD_PLACE_COUNT - 1);
        const float d = Saturate(density), pn = Saturate(panic);
        const float level = 0.6f * powf(d, 0.7f);  // bed RMS ~0.1: -24 dBFS for a packed crowd
        const float k = 1.f - expf(-blockSec / 1.2f);
        bool any = false;
        for (int p = 0; p < CROWD_PLACE_COUNT; p++) {
            float tg = p == place ? level * (1.f - 0.85f * pn) : 0.f;
            gain[p] += (tg - gain[p]) * k;
            if (gain[p] < 1e-4f && tg == 0.f) gain[p] = 0.f;
            any = any || gain[p] > 0.f;
        }
        // panic swells quickly and dies away slowly
        float ptg = pn * 0.9f * powf(Max(d, 0.15f), 0.6f);
        panicGain += (ptg - panicGain) * (1.f - expf(-blockSec / (ptg > panicGain ? 0.25f : 3.f)));
        if (panicGain < 1e-4f && ptg == 0.f) panicGain = 0.f;
        any = any || panicGain > 0.f;
        if (!any) return;
        // muffling: outdoor crowds heard from inside a building or a closed car; indoor crowds only from a car
        bool indoorPlace = place == CROWD_CLUB || place == CROWD_MALL;
        float m = indoorPlace ? 0.8f * Saturate(inVehicle) : Max(Saturate(interior), 0.8f * Saturate(inVehicle));
        float fc = Lerp(16000.f, 700.f, m);
        if (fabsf(fc - mufFc) > 50.f) {
            mufFc = fc;
            mufL.set(fc);
            mufR.set(fc);
        }
        const float att = 1.f - 0.55f * m;
        for (int p = 0; p < CROWD_PLACE_COUNT; p++) {
            if (gain[p] <= 0.f) continue;
            if (offline && !g_beds[p][0].ready.load(std::memory_order_acquire)) renderBed(p, 0);
            int readyCount = 0;
            for (int s = 0; s < kSeeds; s++) readyCount += g_beds[p][s].ready.load(std::memory_order_acquire) ? 1 : 0;
            for (int s = 0; s < kSeeds && readyCount > 0; s++) {
                const Bed& b = g_beds[p][s];
                if (!b.ready.load(std::memory_order_acquire) || b.pcm.size() < 4) continue;
                // two seeds are panned apart for width; a lone seed plays in the centre
                float gl = readyCount == 1 ? 0.7f : (s == 0 ? 0.85f : 0.4f);
                float gr = readyCount == 1 ? 0.7f : (s == 0 ? 0.4f : 0.85f);
                playBed(b, pos[p][s], gain[p] * att, gl, gr, L, R, n);
            }
        }
        if (panicGain > 0.f) {
            if (offline && !g_panic[0].ready.load(std::memory_order_acquire)) renderPanic(0);
            int readyCount = 0;
            for (int s = 0; s < kSeeds; s++) readyCount += g_panic[s].ready.load(std::memory_order_acquire) ? 1 : 0;
            for (int s = 0; s < kSeeds && readyCount > 0; s++) {
                const Bed& b = g_panic[s];
                if (!b.ready.load(std::memory_order_acquire) || b.pcm.size() < 4) continue;
                float gl = readyCount == 1 ? 0.7f : (s == 0 ? 0.8f : 0.45f);
                float gr = readyCount == 1 ? 0.7f : (s == 0 ? 0.45f : 0.8f);
                playBed(b, panicPos[s], panicGain * att, gl, gr, L, R, n);
            }
        }
        if (m > 0.01f)
            for (int i = 0; i < n; i++) L[i] = mufL.process(L[i]), R[i] = mufR.process(R[i]);
    }
};

}  // namespace crowd

void crowdStart(bool async) {
    crowd::g_stop.store(false);
    if (async && !crowd::g_thread.joinable()) crowd::g_thread = std::thread(crowd::renderAll);
}

void crowdStop() {
    crowd::g_stop.store(true);
    if (crowd::g_thread.joinable()) crowd::g_thread.join();
}

}  // namespace detail
}  // namespace Audio
