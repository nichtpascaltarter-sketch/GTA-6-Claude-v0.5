// Native (Linux) test harness for the audio engine. Renders WAV files of every Sfx, engine sweeps,
// sirens and other emitters, a 60 s excerpt of every radio station, the score at several
// intensities and ambience presets, and runs objective checks (NaN/Inf, peak <= 1, DC offset,
// sample-to-sample discontinuities, loudness per category, spectral sanity).
//
// Build (from the repo root):
//   g++ -std=c++17 -O2 -I src tests/audio/test_audio.cpp -o /tmp/test_audio -lpthread
//   /tmp/test_audio [outdir] [--quick]
// Define AUDIO_TEST_SPEECH_STUB to link a trivial speech stub instead of src/audio/speech.cpp.
#include <cstdarg>
#include <chrono>
#include <map>
#include <sys/stat.h>
#include <time.h>
#include "audio/audio_all.cpp"
#ifdef AUDIO_TEST_SPEECH_STUB
#include "speech_stub.cpp"
#else
#include "audio/speech.cpp"
#endif

void LogPrintf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}
void FatalError(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
    exit(1);
}
std::string StrFormat(const char* fmt, ...) {
    char b[4096];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    return b;
}
// Wall clock normally; per-thread CPU time while the perf test runs (the test machine is shared, so wall time
// would count the time this thread spends descheduled).
static bool g_cpuClock = false;
double TimeSeconds() {
    if (g_cpuClock) {
        timespec ts;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    }
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

using namespace Audio;

static std::string g_out = "/tmp/audio_test";
static int g_fail = 0, g_checks = 0;
static FILE* g_report = nullptr;

static void check(bool ok, const char* what, const std::string& detail = "") {
    g_checks++;
    if (!ok) {
        g_fail++;
        printf("  FAIL: %s %s\n", what, detail.c_str());
        if (g_report) fprintf(g_report, "FAIL %s %s\n", what, detail.c_str());
    }
}

static void writeWav(const std::string& path, const std::vector<float>& inter, int channels) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    int n = (int)inter.size() / channels;
    int dataBytes = n * channels * 2, riff = 36 + dataBytes, fmtLen = 16, sr = 48000, br = sr * channels * 2;
    short fmt = 1, ch = (short)channels, ba = (short)(channels * 2), bits = 16;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtLen, 4, 1, f);
    fwrite(&fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f);
    fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&dataBytes, 4, 1, f);
    std::vector<short> s(inter.size());
    for (size_t i = 0; i < inter.size(); i++) s[i] = (short)Clamp((int)lrintf(inter[i] * 32767.f), -32767, 32767);
    fwrite(s.data(), 2, s.size(), f);
    fclose(f);
}

struct Stats {
    float peak = 0, rmsDb = -120, dc = 0, maxJump = 0, crest = 0;
    bool finite = true;
    double loudEnergy = 0;
};
static Stats analyze(const std::vector<float>& x, int ch) {
    Stats s;
    double sum = 0, sumsq = 0;
    size_t n = x.size();
    for (size_t i = 0; i < n; i++) {
        float v = x[i];
        if (!std::isfinite(v)) s.finite = false;
        s.peak = Max(s.peak, fabsf(v));
        sum += v;
        sumsq += (double)v * v;
        if (i >= (size_t)ch) s.maxJump = Max(s.maxJump, fabsf(v - x[i - (size_t)ch]));
    }
    double mean = n ? sum / (double)n : 0;
    double rms = n ? sqrt(sumsq / (double)n) : 0;
    s.dc = (float)mean;
    s.rmsDb = (float)(20.0 * log10(rms + 1e-12));
    s.crest = (float)(20.0 * log10((s.peak + 1e-12) / (rms + 1e-12)));
    return s;
}

// RMS over the loudest 400 ms window (dB) - a robust "event loudness" for one-shots.
static float loudestWindowDb(const std::vector<float>& x, int ch, float win = 0.4f) {
    int n = (int)x.size() / ch, w = (int)(win * 48000.f);
    if (n <= 0) return -120.f;
    std::vector<double> e((size_t)n + 1, 0.0);
    for (int i = 0; i < n; i++) {
        double s = 0;
        for (int c = 0; c < ch; c++) s += (double)x[(size_t)i * ch + c] * x[(size_t)i * ch + c];
        e[(size_t)i + 1] = e[(size_t)i] + s / ch;
    }
    double best = 0;
    for (int i = 0; i + 1 < n; i += 240) {
        int j = Min(n, i + w);
        best = Max(best, (e[(size_t)j] - e[(size_t)i]) / (double)Max(1, j - i));
    }
    return (float)(10.0 * log10(best + 1e-12));
}

// Band energy fraction (dB relative to total) via a Goertzel-free approach: simple DFT on frames.
static void bandEnergies(const std::vector<float>& mono, float* out, const float* edges, int nb) {
    const int N = 4096;
    std::vector<double> P(N / 2 + 1, 0.0);
    std::vector<float> win(N);
    for (int i = 0; i < N; i++) win[(size_t)i] = 0.5f - 0.5f * cosf(kTwoPi * (float)i / (float)N);
    int frames = 0;
    std::vector<double> re(N), im(N);
    for (size_t st = 0; st + N <= mono.size(); st += N) {
        // radix-2 FFT
        for (int i = 0; i < N; i++) { re[(size_t)i] = mono[st + (size_t)i] * win[(size_t)i]; im[(size_t)i] = 0; }
        for (int i = 1, j = 0; i < N; i++) {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap(re[(size_t)i], re[(size_t)j]); std::swap(im[(size_t)i], im[(size_t)j]); }
        }
        for (int len = 2; len <= N; len <<= 1) {
            double ang = -2 * 3.14159265358979 / len;
            double wr = cos(ang), wi = sin(ang);
            for (int i = 0; i < N; i += len) {
                double cr = 1, ci = 0;
                for (int k = 0; k < len / 2; k++) {
                    double ur = re[(size_t)(i + k)], ui = im[(size_t)(i + k)];
                    double vr = re[(size_t)(i + k + len / 2)] * cr - im[(size_t)(i + k + len / 2)] * ci;
                    double vi = re[(size_t)(i + k + len / 2)] * ci + im[(size_t)(i + k + len / 2)] * cr;
                    re[(size_t)(i + k)] = ur + vr; im[(size_t)(i + k)] = ui + vi;
                    re[(size_t)(i + k + len / 2)] = ur - vr; im[(size_t)(i + k + len / 2)] = ui - vi;
                    double nr = cr * wr - ci * wi;
                    ci = cr * wi + ci * wr;
                    cr = nr;
                }
            }
        }
        for (int k = 0; k <= N / 2; k++) P[(size_t)k] += re[(size_t)k] * re[(size_t)k] + im[(size_t)k] * im[(size_t)k];
        frames++;
    }
    double tot = 1e-20;
    for (double p : P) tot += p;
    for (int b = 0; b < nb; b++) {
        double s = 1e-20;
        for (int k = 0; k <= N / 2; k++) {
            float f = (float)k * 48000.f / (float)N;
            if (f >= edges[b] && f < edges[b + 1]) s += P[(size_t)k];
        }
        out[b] = (float)(10.0 * log10(s / tot));
    }
}

static std::vector<float> render(float seconds) {
    int n = (int)(seconds * 48000.f);
    std::vector<float> buf((size_t)n * 2);
    renderOffline(buf.data(), n);
    return buf;
}

static void resetWorld() {
    Listener l;
    l.pos = vec3(0, 0, 0);
    l.forward = vec3(0, 1, 0);
    l.up = vec3(0, 0, 1);
    update(l, 0.016f);
    Ambience a;
    a.urban = 0.f; a.nature = 0.f; a.coast = 0.f; a.wetland = 0.f; a.rain = 0.f; a.wind = 0.f; a.timeOfDay = 12.f; a.underwater = 0.f;
    setAmbience(a);
    setRadioStation(-1);
    setScore(0, 0.f);
    setPaused(false);
    setSlowMotion(1.f);
    render(0.5f);
}

static void save(const std::string& name, const std::vector<float>& buf) { writeWav(g_out + "/" + name + ".wav", buf, 2); }

static void basicChecks(const std::string& name, const std::vector<float>& buf, float maxJump = 0.9f) {
    Stats s = analyze(buf, 2);
    check(s.finite, "finite", name);
    check(s.peak <= 1.0f, "peak<=1", name + StrFormat(" peak=%.3f", s.peak));
    check(fabsf(s.dc) < 0.01f, "dc", name + StrFormat(" dc=%.4f", s.dc));
    check(s.maxJump < maxJump, "discontinuity", name + StrFormat(" maxJump=%.3f", s.maxJump));
}

// Fraction of energy (0..1) between f0 and f1 Hz, via a direct DFT over the first 4096-sample frames.
static float bandFraction(const std::vector<float>& x, float f0, float f1) {
    const float edges[3] = {f0, f1, 24000.f};
    std::vector<float> m(x);
    if (m.size() < 4096) m.resize(4096, 0.f);
    float out[2];
    bandEnergies(m, out, edges, 1);
    return powf(10.f, out[0] / 10.f);
}

static void kitTests() {
    using namespace detail::music;
    static const char* kStyles[] = {"retro80s", "trap", "boombap", "dembow", "house", "rock", "jazz", "country", "lofi", "score"};
    for (int st = 0; st < 10; st++) {
        DrumKit kit;
        bool used[DK_COUNT];
        for (bool& u : used) u = true;
        buildKit(kit, (KitStyle)st, 120.f, 1234u, used);
        const std::vector<float>& kick = kit.s[DK_KICK].d[0];
        const std::vector<float>& hat = kit.s[DK_HAT_C].d[0];
        const std::vector<float>& ohat = kit.s[DK_HAT_O].d[0];
        const std::vector<float>& snare = kit.s[DK_SNARE].d[0];
        float kLow = bandFraction(kick, 20.f, 150.f), hHigh = bandFraction(hat, 5000.f, 24000.f), ohHigh = bandFraction(ohat, 5000.f, 24000.f);
        float sMid = bandFraction(snare, 150.f, 10000.f);
        printf("  kit %-9s kick<150Hz %.0f%%  closedHat>5k %.0f%%  openHat>5k %.0f%%  snare 150-10k %.0f%%\n", kStyles[st], kLow * 100, hHigh * 100,
               ohHigh * 100, sMid * 100);
        check(kLow > 0.55f, "kick energy below 150 Hz", kStyles[st]);
        check(hHigh > 0.5f, "hat energy above 5 kHz", kStyles[st]);
        check(ohHigh > 0.5f, "open hat energy above 5 kHz", kStyles[st]);
        check(sMid > 0.8f, "snare energy 150 Hz-10 kHz", kStyles[st]);
        for (int pc = 0; pc < DK_COUNT; pc++) {
            for (int v = 0; v < kit.s[pc].vars; v++) {
                Stats s2;
                std::vector<float> st2;
                for (float f : kit.s[pc].d[v]) { st2.push_back(f); st2.push_back(f); }
                s2 = analyze(st2, 2);
                check(s2.finite && s2.peak <= 1.f, "kit piece finite/peak", StrFormat("%s piece %d", kStyles[st], pc));
            }
        }
    }
}

// Detects clicks at mixer block boundaries: compares the second difference at multiples of the
// block size with the distribution elsewhere.
static void boundaryClickCheck(const std::string& name, const std::vector<float>& buf) {
    int n = (int)buf.size() / 2;
    double bmax = 0, osum = 0;
    int ocnt = 0;
    std::vector<float> d2;
    d2.reserve((size_t)n);
    for (int i = 2; i < n; i++) {
        float v = 0;
        for (int c = 0; c < 2; c++) v = Max(v, fabsf(buf[(size_t)i * 2 + c] - 2.f * buf[(size_t)(i - 1) * 2 + c] + buf[(size_t)(i - 2) * 2 + c]));
        if (i % detail::kMaxBlock == 0) bmax = Max(bmax, (double)v);
        else { d2.push_back(v); osum += v; ocnt++; }
    }
    std::sort(d2.begin(), d2.end());
    float p999 = d2.empty() ? 0.f : d2[(size_t)(d2.size() * 0.999)];
    printf("  %-24s boundary max d2 %.4f vs interior p99.9 %.4f\n", name.c_str(), bmax, p999);
    check(bmax <= Max(p999 * 1.5f, 0.02f), "no block-boundary clicks", name);
}

// Busy-scene CPU benchmark: city+rain ambience, in-car radio, score, 20 engines and 8 other emitters,
// ~30 one-shots per second. Times the mixer (audio thread work) and music production separately.
// ---------------------------------------------------------------------------------------------
// Virtual geometry for the environment tests (the game registers its collision-world raycast instead).
enum VScene { VS_OPEN = 0, VS_STREET, VS_ROOM, VS_TUNNEL, VS_STATION, VS_COUNT, VS_BUILDING = VS_COUNT, VS_LOWWALL };
static const char* kSceneName[VS_COUNT] = {"open", "street", "room", "tunnel", "station"};
struct VBox {
    vec3 mn, mx;
};
static std::vector<VBox> g_vboxes;
static bool vRay(vec3 o, vec3 d, float maxD, float* hit) {
    float best = maxD;
    bool any = false;
    for (const VBox& b : g_vboxes) {
        float t0 = 0.f, t1 = maxD;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; a++) {
            float oa = a == 0 ? o.x : a == 1 ? o.y : o.z, da = a == 0 ? d.x : a == 1 ? d.y : d.z;
            float lo = a == 0 ? b.mn.x : a == 1 ? b.mn.y : b.mn.z, hi = a == 0 ? b.mx.x : a == 1 ? b.mx.y : b.mx.z;
            if (fabsf(da) < 1e-8f) {
                if (oa < lo || oa > hi) miss = true;
                continue;
            }
            float ta = (lo - oa) / da, tb = (hi - oa) / da;
            if (ta > tb) std::swap(ta, tb);
            t0 = Max(t0, ta);
            t1 = Min(t1, tb);
            if (t0 > t1) miss = true;
        }
        if (!miss && t0 < best) {
            best = t0;
            any = true;
        }
    }
    if (any) *hit = best;
    return any;
}
static void setScene(int sc) {
    g_vboxes.clear();
    switch (sc) {
        case VS_STREET:  // 18 m wide street between 32 m blocks
            g_vboxes.push_back({vec3(-70, -400, 0), vec3(-9, 400, 32)});
            g_vboxes.push_back({vec3(9, -400, 0), vec3(70, 400, 32)});
            break;
        case VS_ROOM:  // 8 x 6 x 3 m room
            g_vboxes.push_back({vec3(-4.3f, -3.3f, -0.3f), vec3(-4.f, 3.3f, 3.3f)});
            g_vboxes.push_back({vec3(4.f, -3.3f, -0.3f), vec3(4.3f, 3.3f, 3.3f)});
            g_vboxes.push_back({vec3(-4.3f, -3.3f, -0.3f), vec3(4.3f, -3.f, 3.3f)});
            g_vboxes.push_back({vec3(-4.3f, 3.f, -0.3f), vec3(4.3f, 3.3f, 3.3f)});
            g_vboxes.push_back({vec3(-4.3f, -3.3f, 3.f), vec3(4.3f, 3.3f, 3.3f)});
            break;
        case VS_TUNNEL:  // 12 m wide, 7 m high road tunnel
            g_vboxes.push_back({vec3(-7, -500, 0), vec3(-6, 500, 8)});
            g_vboxes.push_back({vec3(6, -500, 0), vec3(7, 500, 8)});
            g_vboxes.push_back({vec3(-7, -500, 7), vec3(7, 500, 8)});
            break;
        case VS_STATION:  // platform roof on open sides
            g_vboxes.push_back({vec3(-8, -60, 6), vec3(8, 60, 6.4f)});
            break;
        case VS_BUILDING:  // a 20 m high block between the listener and the source 40 m ahead
            g_vboxes.push_back({vec3(-15, 12, 0), vec3(15, 30, 20)});
            break;
        case VS_LOWWALL:  // a 3 m wall: the sound goes over it
            g_vboxes.push_back({vec3(-15, 12, 0), vec3(15, 13, 3)});
            break;
        default: break;
    }
}
static Listener sceneListener(float interior) {
    Listener l;
    l.pos = vec3(0, 0, 1.7f);
    l.forward = vec3(0, 1, 0);
    l.up = vec3(0, 0, 1);
    l.interior = interior;
    return l;
}
static void settleScene(int sc, float interior, float urban) {
    setScene(sc);
    setRaycast(vRay);
    Ambience a;
    a.urban = urban * 0.f; a.nature = 0.f;  // beds off: the environment is judged from the geometry alone a.coast = 0.f; a.wetland = 0.f; a.rain = 0.f; a.wind = 0.f; a.timeOfDay = 12.f; a.underwater = 0.f;
    setAmbience(a);
    Listener l = sceneListener(interior);
    for (int f = 0; f < 150; f++) {
        update(l, 1.f / 60.f);
        render(1.f / 60.f);
    }
}
static std::vector<float> renderScene(float secs, float interior) {
    Listener l = sceneListener(interior);
    std::vector<float> all;
    int frames = (int)(secs * 60.f + 0.5f);
    for (int f = 0; f < frames; f++) {
        update(l, 1.f / 60.f);
        std::vector<float> b = render(1.f / 60.f);
        all.insert(all.end(), b.begin(), b.end());
    }
    return all;
}
// Energy (dB) of both channels in [t0, t1) seconds.
static float windowDb(const std::vector<float>& x, float t0, float t1) {
    size_t a = (size_t)(t0 * 48000.f) * 2, b = Min(x.size(), (size_t)(t1 * 48000.f) * 2);
    double e = 0;
    for (size_t i = a; i < b; i++) e += (double)x[i] * x[i];
    return (float)(10.0 * log10(e / (double)Max((size_t)1, b - a) + 1e-14));
}
// First time |x| exceeds rel * peak.
static float onsetTime(const std::vector<float>& x, float rel) {
    float pk = 0.f;
    for (float v : x) pk = Max(pk, fabsf(v));
    for (size_t i = 0; i < x.size(); i++)
        if (fabsf(x[i]) > rel * pk) return (float)(i / 2) / 48000.f;
    return -1.f;
}
// Reverberation time from the Schroeder energy decay curve after the loudest point (T20 x 3), with the
// steady background power (measured over the last 0.3 s) subtracted.
static float decayRt(const std::vector<float>& x) {
    size_t n = x.size() / 2, pk = 0;
    float pv = 0.f;
    for (size_t i = 0; i < n; i++) {
        float v = fabsf(x[i * 2]) + fabsf(x[i * 2 + 1]);
        if (v > pv) { pv = v; pk = i; }
    }
    double bg = 0.0;
    size_t nb = Min(n, (size_t)14400);
    for (size_t i = n - nb; i < n; i++) bg += (double)x[i * 2] * x[i * 2] + (double)x[i * 2 + 1] * x[i * 2 + 1];
    bg /= (double)Max(nb, (size_t)1);
    std::vector<double> edc(n + 1, 0.0);
    for (size_t i = n; i-- > pk;) edc[i] = edc[i + 1] + Max(0.0, (double)x[i * 2] * x[i * 2] + (double)x[i * 2 + 1] * x[i * 2 + 1] - bg);
    double e0 = edc[pk] + 1e-30;
    float t5 = -1.f, t25 = -1.f;
    for (size_t i = pk; i < n; i++) {
        double db = 10.0 * log10(edc[i] / e0 + 1e-30);
        if (t5 < 0.f && db <= -5.0) t5 = (float)(i - pk) / 48000.f;
        if (t25 < 0.f && db <= -25.0) { t25 = (float)(i - pk) / 48000.f; break; }
    }
    return (t5 >= 0.f && t25 > t5) ? 3.f * (t25 - t5) : -1.f;
}

static void envTests() {
    printf("== Environment acoustics and gunfire\n");
    const float interiorOf[VS_COUNT] = {0.f, 0.f, 1.f, 0.f, 0.f};
    const float urbanOf[VS_COUNT] = {0.f, 1.f, 0.f, 0.8f, 0.8f};
    float rt[VS_COUNT], slap[VS_COUNT], late[VS_COUNT];
    AcousticState zone[VS_COUNT];
    for (int sc = 0; sc < VS_COUNT; sc++) {
        settleScene(sc, interiorOf[sc], urbanOf[sc]);
        playGunshot(SFX_PISTOL, vec3(0.2f, 0.4f, 1.5f), vec3(0, 1, 0), GUN_PLAYER);
        std::vector<float> b = renderScene(4.f, interiorOf[sc]);
        basicChecks(std::string("env_fp_pistol_") + kSceneName[sc], b, 1.95f);
        save(std::string("env_fp_pistol_") + kSceneName[sc], b);
        slap[sc] = windowDb(b, 0.045f, 0.075f) - windowDb(b, 0.f, 0.02f);
        renderScene(2.5f, interiorOf[sc]);
        // decay of the space: an NPC shot 30 m off (no action noise, casing drops etc.)
        playGunshot(SFX_PISTOL, vec3(0.f, 30.f, 1.5f), vec3(1, 0, 0), 0);
        std::vector<float> d = renderScene(5.f, interiorOf[sc]);
        rt[sc] = decayRt(d);
        late[sc] = windowDb(d, 0.35f, 1.2f) - windowDb(d, 0.05f, 0.14f);
        save(std::string("env_npc30m_pistol_") + kSceneName[sc], d);
        const AcousticState& ac = detail::mix::g_mixer->envfx.cur;
        printf("  %-8s player pistol: 45-75 ms energy %.1f dB re direct, loud %.1f dB; NPC 30 m late/early %.1f dB\n", kSceneName[sc], slap[sc],
               loudestWindowDb(b, 2), late[sc]);
        printf("           zone: enclosed %.2f canyon %.2f cover %.2f  rtIn %.2f rtOut %.2f wetIn %.2f wetOut %.2f er %.2f echo %.2f flutter %.2f\n",
               ac.enclosed, ac.canyon, detail::mix::g_mixer->envfx.tgt.cover, ac.rtIn, ac.rtOut, ac.wetIn, ac.wetOut, ac.er, ac.echo,
               detail::mix::g_mixer->envfx.flFb);
        zone[sc] = ac;
        renderScene(2.5f, interiorOf[sc]);
    }
    (void)rt;
    check(zone[VS_STREET].canyon > 0.6f && zone[VS_OPEN].canyon < 0.05f, "street canyon detected", StrFormat("%.2f", zone[VS_STREET].canyon));
    check(zone[VS_STREET].rtOut > zone[VS_OPEN].rtOut + 0.5f, "street reverberates longer than open ground",
          StrFormat("%.2f vs %.2f", zone[VS_STREET].rtOut, zone[VS_OPEN].rtOut));
    check(zone[VS_TUNNEL].enclosed > 0.7f && zone[VS_TUNNEL].rtIn > 2.5f, "tunnel: enclosed, long reverb", StrFormat("%.2f", zone[VS_TUNNEL].rtIn));
    check(zone[VS_ROOM].enclosed > 0.9f && zone[VS_ROOM].rtIn > 0.25f && zone[VS_ROOM].rtIn < 0.9f, "room: enclosed, short reverb",
          StrFormat("%.2f", zone[VS_ROOM].rtIn));
    check(zone[VS_STATION].enclosed > 0.2f && zone[VS_STATION].enclosed < 0.8f && zone[VS_STATION].rtIn < zone[VS_TUNNEL].rtIn,
          "station: partly covered, shorter than a tunnel", StrFormat("%.2f %.2f", zone[VS_STATION].enclosed, zone[VS_STATION].rtIn));
    check(zone[VS_OPEN].echo > 0.4f && zone[VS_ROOM].echo < 0.05f, "open-field echo outdoors only");
    check(late[VS_TUNNEL] > late[VS_STREET] && late[VS_STREET] > late[VS_ROOM], "late energy: tunnel > street > room",
          StrFormat("%.1f %.1f %.1f", late[VS_TUNNEL], late[VS_STREET], late[VS_ROOM]));
    check(slap[VS_STREET] > slap[VS_OPEN] + 4.f, "street slap-back between the facades", StrFormat("%.1f vs %.1f dB", slap[VS_STREET], slap[VS_OPEN]));

    // speed of sound: an NPC shot 170 m away arrives ~0.5 s later, duller than a close one
    settleScene(VS_OPEN, 0.f, 0.f);
    playGunshot(SFX_PISTOL, vec3(0, 170, 1.7f), vec3(0, -1, 0), 0);
    std::vector<float> far = renderScene(3.5f, 0.f);
    float on = onsetTime(far, 0.03f);
    printf("  open: pistol at 170 m arrives after %.3f s (expected %.3f)\n", on, 170.f / 343.f);
    check(fabsf(on - 170.f / 343.f) < 0.02f, "propagation delay", StrFormat("%.3f", on));
    basicChecks("env_npc_pistol_170m_open", far, 1.6f);
    save("env_npc_pistol_170m_open", far);
    // near vs player vs suppressed in the street
    settleScene(VS_STREET, 0.f, 1.f);
    playGunshot(SFX_RIFLE, vec3(-3, 9.5f, 1.5f), vec3(1, 0, 0), 0);
    std::vector<float> npc = renderScene(3.f, 0.f);
    playGunshot(SFX_RIFLE, vec3(0.2f, 0.4f, 1.5f), vec3(0, 1, 0), GUN_PLAYER);
    std::vector<float> fp = renderScene(3.f, 0.f);
    playGunshot(SFX_RIFLE, vec3(-3, 9.5f, 1.5f), vec3(1, 0, 0), GUN_SUPPRESSED);
    std::vector<float> sup = renderScene(3.f, 0.f);
    playGunshot(SFX_PISTOL, vec3(-3, 9.5f, 1.5f), vec3(1, 0, 0), GUN_SUPPRESSED);
    std::vector<float> supP = renderScene(3.f, 0.f);
    playGunshot(SFX_PISTOL, vec3(-3, 9.5f, 1.5f), vec3(1, 0, 0), 0);
    std::vector<float> npcP = renderScene(3.f, 0.f);
    playGunshot(SFX_RIFLE, vec3(-40, 390, 1.5f), vec3(1, 0, 0), 0);
    std::vector<float> dist = renderScene(4.5f, 0.f);
    float lNpc = loudestWindowDb(npc, 2, 0.1f), lFp = loudestWindowDb(fp, 2, 0.1f), lSup = loudestWindowDb(sup, 2, 0.1f);
    float lSupP = loudestWindowDb(supP, 2, 0.1f), lNpcP = loudestWindowDb(npcP, 2, 0.1f), lDist = loudestWindowDb(dist, 2, 0.1f);
    printf("  street: rifle player %.1f dB, NPC at 10 m %.1f dB, suppressed %.1f dB, 390 m away %.1f dB; pistol %.1f / suppressed %.1f dB\n", lFp,
           lNpc, lSup, lDist, lNpcP, lSupP);
    check(lFp > lNpc + 3.f, "player's shot louder than an NPC's 10 m away", StrFormat("%.1f vs %.1f", lFp, lNpc));
    check(lSupP < lNpcP - 10.f, "suppressed pistol much quieter", StrFormat("%.1f vs %.1f", lSupP, lNpcP));
    check(lSup < lNpc - 4.f, "suppressed rifle quieter (still cracks)", StrFormat("%.1f vs %.1f", lSup, lNpc));
    check(lDist < lNpc - 12.f && lDist > -60.f, "distant shot quieter but audible", StrFormat("%.1f", lDist));
    float hiNear = bandFraction(std::vector<float>(npc.begin(), npc.begin() + Min(npc.size(), (size_t)48000)), 2000.f, 20000.f);
    std::vector<float> distWin(dist.begin() + (size_t)(1.0f * 48000.f) * 2, dist.begin() + (size_t)(2.5f * 48000.f) * 2);
    float hiFar = bandFraction(distWin, 2000.f, 20000.f);
    printf("  energy above 2 kHz: near %.1f%%, 390 m %.1f%%\n", hiNear * 100.f, hiFar * 100.f);
    check(hiFar < hiNear * 0.5f, "distant shot is duller (boom)", StrFormat("%.3f vs %.3f", hiFar, hiNear));
    for (auto* v : {&npc, &fp, &sup, &supP, &npcP, &dist}) basicChecks("env_street_shots", *v, 1.95f);
    save("env_street_rifle_npc10m", npc);
    save("env_street_rifle_player", fp);
    save("env_street_rifle_suppressed", sup);
    save("env_street_pistol_suppressed", supP);
    save("env_street_rifle_390m", dist);
    // supersonic crack: a sniper round passing 3 m away arrives well before the muzzle report
    settleScene(VS_OPEN, 0.f, 0.f);
    playGunshot(SFX_SNIPER, vec3(3, -300, 1.7f), vec3(0, 1, 0), 0);
    std::vector<float> cr = renderScene(3.f, 0.f);
    float onC = onsetTime(cr, 0.05f);
    float eCrack = windowDb(cr, 0.36f, 0.42f), eGap = windowDb(cr, 0.6f, 0.8f), eRep = windowDb(cr, 0.86f, 1.0f);
    printf("  sniper round passing 3 m away: first arrival %.3f s (crack), crack %.1f dB, gap %.1f dB, report %.1f dB\n", onC, eCrack, eGap, eRep);
    check(onC > 0.33f && onC < 0.42f, "crack arrives before the report", StrFormat("%.3f", onC));
    check(eRep > eGap + 6.f, "report follows at the speed of sound");
    basicChecks("env_sniper_crack", cr, 1.6f);
    save("env_sniper_crack", cr);
    // occlusion: a car engine and a gunshot behind a building / behind a low wall
    {
        float lvl[3], hi[3], gun[3], gunHi[3];
        const int scenes[3] = {VS_OPEN, VS_BUILDING, VS_LOWWALL};
        for (int k = 0; k < 3; k++) {
            settleScene(scenes[k], 0.f, 0.f);
            EmitterHandle e = createEmitter(EMIT_ENGINE);
            Listener l = sceneListener(0.f);
            std::vector<float> eng;
            for (int f = 0; f < 150; f++) {
                update(l, 1.f / 60.f);
                setEmitter(e, vec3(0, 40, 0.5f), vec3(), 0.55f, 0.7f, 0.6f, (float)ENGINE_V8, 1.f);
                std::vector<float> b = render(1.f / 60.f);
                if (f >= 60) eng.insert(eng.end(), b.begin(), b.end());
            }
            destroyEmitter(e);
            renderScene(0.5f, 0.f);
            lvl[k] = analyze(eng, 2).rmsDb;
            hi[k] = bandFraction(eng, 1500.f, 20000.f);
            playGunshot(SFX_RIFLE, vec3(0, 40, 1.5f), vec3(1, 0, 0), 0);
            std::vector<float> g = renderScene(2.f, 0.f);
            gun[k] = loudestWindowDb(g, 2, 0.1f);
            gunHi[k] = bandFraction(std::vector<float>(g.begin() + 12000, g.begin() + 12000 + 24000), 2000.f, 20000.f);
            save(std::string("env_occlusion_engine_") + (k == 0 ? "clear" : k == 1 ? "building" : "lowwall"), eng);
            save(std::string("env_occlusion_rifle_") + (k == 0 ? "clear" : k == 1 ? "building" : "lowwall"), g);
            basicChecks("env_occlusion", eng);
            basicChecks("env_occlusion_gun", g, 1.95f);
        }
        printf("  occlusion (clear / behind a building / behind a low wall): engine %.1f / %.1f / %.1f dB (HF %.1f / %.1f / %.1f %%), "
               "rifle %.1f / %.1f / %.1f dB\n", lvl[0], lvl[1], lvl[2], hi[0] * 100.f, hi[1] * 100.f, hi[2] * 100.f, gun[0], gun[1], gun[2]);
        check(lvl[1] < lvl[0] - 5.f && hi[1] < hi[0] * 0.6f, "engine behind a building: quieter and duller");
        check(lvl[2] < lvl[0] - 1.f && lvl[2] > lvl[1] + 1.f, "a low wall occludes partially");
        check(gun[1] < gun[0] - 4.f && gunHi[1] < gunHi[0], "gunshot behind a building: quieter and duller");
    }

    // a short gunfight in the street: NPCs at various distances, the player answering
    settleScene(VS_STREET, 0.f, 1.f);
    {
        Listener l = sceneListener(0.f);
        std::vector<float> all;
        Rng r(77);
        for (int f = 0; f < 60 * 8; f++) {
            update(l, 1.f / 60.f);
            if (f % 9 == 0 && r.chance(0.6f)) {
                Sfx w = r.chance(0.5f) ? SFX_RIFLE : SFX_PISTOL;
                vec3 p(r.range(-8.f, 8.f), r.range(20.f, 120.f), 1.5f);
                playGunshot(w, p, normalize(l.pos - p + vec3(r.range(-4.f, 4.f), 0, 0)), 0);
            }
            if (f % 12 == 5 && f > 120) playGunshot(SFX_SMG, vec3(0.2f, 0.4f, 1.5f), vec3(0, 1, 0), GUN_PLAYER);
            std::vector<float> b = render(1.f / 60.f);
            all.insert(all.end(), b.begin(), b.end());
        }
        basicChecks("env_street_gunfight", all, 1.95f);
        save("env_street_gunfight", all);
    }
    setRaycast(nullptr);
    setScene(VS_OPEN);
    resetWorld();
}

static void perfTest(float seconds, int engines = 20, int others = 8) {
    resetWorld();
    dsp::ScopedFlushDenormals ftz;  // as on the real audio / music threads
    Ambience a;
    a.urban = 1.f; a.rain = 0.6f; a.wind = 0.4f; a.nature = 0.3f; a.timeOfDay = 21.f;
    setAmbience(a);
    setRadioStation(6);
    setScore(3, 0.8f);
    setScene(VS_STREET);
    setRaycast(vRay);
    std::vector<EmitterHandle> em;
    Rng r(5);
    for (int i = 0; i < engines; i++) em.push_back(createEmitter(EMIT_ENGINE));
    EmitterType otherTypes[8] = {EMIT_SIREN, EMIT_ROTOR, EMIT_FIRE, EMIT_CROWD, EMIT_HORN, EMIT_TIRE_SKID, EMIT_WIND_RUSH, EMIT_RADIO_WORLD};
    for (int i = 0; i < others; i++) em.push_back(createEmitter(otherTypes[i]));
    render(1.f);
    g_cpuClock = true;
    double tMix = 0, tMusic = 0;
    int blocks = (int)(seconds * 48000.f / 256.f);
    float out[512];
    for (int b = 0; b < blocks; b++) {
        if (b % 3 == 0) {  // ~60 Hz game updates
            float t = (float)b * 256.f / 48000.f;
            Listener l;
            l.pos = vec3(0, t * 10.f, 0);
            l.vel = vec3(0, 10.f, 0);
            l.inVehicle = 1.f;
            update(l, 1.f / 60.f);
            for (size_t i = 0; i < em.size(); i++) {
                float ang = (float)i * 0.7f + t * 0.3f;
                vec3 p = l.pos + vec3(cosf(ang) * (8.f + 6.f * (float)i), sinf(ang) * (8.f + 6.f * (float)i), 0);
                float p0 = (int)i < engines ? 0.5f + 0.4f * sinf(t + (float)i) : ((int)i == engines + 7 ? 6.f : 0.7f);
                setEmitter(em[i], p, vec3(10, 0, 0), p0, 0.6f, 0.5f, (float)(i % ENGINE_COUNT), 1.f);
            }
            if (r.chance(0.5f)) play((Sfx)r.irange(SFX_STEP_CONCRETE, SFX_BOAT_SLAM), l.pos + vec3(r.range(-30, 30), r.range(-30, 30), 0));
            if (r.chance(0.15f)) playGunshot(SFX_RIFLE, l.pos + vec3(r.range(-8, 8), r.range(10, 200), 1.5f), vec3(1, 0, 0), 0);
        }
        double t0 = TimeSeconds();
        detail::speechPumpSync();
        detail::musicPumpSync(256);
        double t1 = TimeSeconds();
        detail::mix::g_mixer->render(out, 256);
        double t2 = TimeSeconds();
        tMusic += t1 - t0;
        tMix += t2 - t1;
    }
    g_cpuClock = false;
    for (auto h : em) destroyEmitter(h);
    setRadioStation(-1);
    setScore(0, 0.f);
    setRaycast(nullptr);
    setScene(VS_OPEN);
    printf("  perf (%d engines + %d emitters): mixer %.2f%% of one core, music production (radio + score) %.2f%%\n", engines, others,
           100.0 * tMix / seconds, 100.0 * tMusic / seconds);
    if (g_report) fprintf(g_report, "perf %d+%d mixer %.2f%% music %.2f%%\n", engines, others, 100.0 * tMix / seconds, 100.0 * tMusic / seconds);
#ifdef AUDIO_PROFILE
    printf("    stages: ambience %.2f%%, voices %.2f%%, radio %.2f%%, emitters %.2f%%, reverb %.2f%%\n", 100 * detail::mix::g_prof[0] / seconds,
           100 * detail::mix::g_prof[1] / seconds, 100 * detail::mix::g_prof[2] / seconds, 100 * detail::mix::g_prof[3] / seconds,
           100 * detail::mix::g_prof[4] / seconds);
    for (double& p : detail::mix::g_prof) p = 0;
#endif
    check(tMix / seconds < 0.10, "mixer cpu < 10% of a core");
    check(tMusic / seconds < 0.06, "music cpu < 6% of a core");
}

int main(int argc, char** argv) {
    bool quick = false, perfOnly = false, envOnly = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) quick = true;
        else if (!strcmp(argv[i], "--perf")) perfOnly = true;
        else if (!strcmp(argv[i], "--env")) envOnly = true;
        else g_out = argv[i];
    }
    if (envOnly) {
        mkdir(g_out.c_str(), 0755);
        init();
        render(0.1f);
        resetWorld();
        envTests();
        printf("\n%d checks, %d failures\n", g_checks, g_fail);
        shutdown();
        return g_fail ? 1 : 0;
    }
    if (perfOnly) {
        init();
        render(0.1f);
        perfTest(20.f, 20, 8);
        perfTest(20.f, 6, 2);
        shutdown();
        return 0;
    }
    mkdir(g_out.c_str(), 0755);
    g_report = fopen((g_out + "/report.txt").c_str(), "w");
    double t0 = TimeSeconds();
    bool dev = init();
    printf("init() -> %s (expected false on Linux: null backend)\n", dev ? "true" : "false");
    if (!dev) {
        // No device and no offline rendering yet: sound-producing calls are safe no-ops.
        SoundHandle h0 = play(SFX_PISTOL, vec3(0, 3, 0));
        SoundHandle h1 = play2D(SFX_UI_SELECT);
        EmitterHandle e0 = createEmitter(EMIT_ENGINE);
        setEmitter(e0, vec3(0, 5, 0), vec3(), 0.5f, 0.5f);
        destroyEmitter(e0);
        VoiceParams vp;
        SoundHandle s0 = speak("Hello there.", vp);
        stop(h0);
        setRadioStation(0);
        setScore(3, 0.5f);
        setScore(3, 0.f);
        setRadioStation(-1);
        check(h0 == 0 && h1 == 0 && e0 == 0 && s0 == 0 && !isPlaying(h0), "API calls are no-ops without a device");
        check(radioStationCount() >= 8 && radioStationName(0)[0] != 0, "station metadata available without a device");
        check(estimateSpeechDuration("Hello there, how are you?", vp) > 0.5f, "speech duration estimate without a device");
        check(!init(), "init() retry without a device still fails cleanly");
    }
    render(0.1f);
    double t1 = TimeSeconds();
    printf("bank + first render: %.2f s\n", t1 - t0);
    resetWorld();

    // ---------------------------------------------------------------- SFX
    {
        printf("== SFX\n");
        std::map<int, float> loud;
        for (int id = 1; id < SFX_COUNT; id++) {
            const detail::SoundDef& d = detail::soundDef(id);
            bool ui = d.bus == detail::Bus::Ui;
            SoundHandle h = ui ? play2D((Sfx)id, 1.f) : play((Sfx)id, vec3(0, 3.f, 0), 1.f);
            std::vector<float> buf;
            for (int k = 0; k < 200 && (k < 2 || isPlaying(h)); k++) {
                std::vector<float> b = render(0.05f);
                buf.insert(buf.end(), b.begin(), b.end());
            }
            std::vector<float> tail = render(0.6f);
            buf.insert(buf.end(), tail.begin(), tail.end());
            basicChecks(std::string("sfx_") + d.name, buf, 1.6f);
            float l = loudestWindowDb(buf, 2);
            loud[id] = l;
            check(l > -45.f, "sfx audible", std::string(d.name) + StrFormat(" %.1f dB", l));
            save(std::string("sfx_") + d.name, buf);
            render(2.5f);  // let reverb tails die
            if (g_report) fprintf(g_report, "sfx %-20s loud %.1f dB\n", d.name, l);
        }
        // relative loudness sanity: weapons louder than footsteps; explosion loudest
        check(loud[SFX_PISTOL] > loud[SFX_STEP_CONCRETE] + 6.f, "gun louder than footstep");
        check(loud[SFX_EXPLOSION] > loud[SFX_UI_MOVE] + 6.f, "explosion louder than ui");
    }

    // ---------------------------------------------------------------- Engines (RPM sweep)
    {
        printf("== Engines\n");
        static const char* kNames[ENGINE_COUNT] = {"i4", "v6", "v8", "v12", "truck_diesel", "electric", "bike_sport", "bike_cruiser", "scooter"};
        for (int k = 0; k < ENGINE_COUNT; k++) {
            resetWorld();
            Listener l;
            l.pos = vec3(0, 0, 0);
            update(l, 0.016f);
            EmitterHandle h = createEmitter(EMIT_ENGINE);
            std::vector<float> all;
            // idle 2 s, rev sweep up 4 s under load, lift-off decel 2 s, cruise 2 s
            const int steps = 10 * 60;
            for (int s = 0; s < steps; s++) {
                float t = (float)s / 60.f;
                float rpm, thr, load;
                if (t < 2.f) { rpm = 0.f; thr = 0.05f; load = 0.1f; }
                else if (t < 6.f) { rpm = (t - 2.f) / 4.f; thr = 1.f; load = 0.8f; }
                else if (t < 8.f) { rpm = Max(0.15f, 1.f - (t - 6.f) / 2.f); thr = 0.f; load = 0.f; }
                else { rpm = 0.35f; thr = 0.3f; load = 0.3f; }
                setEmitter(h, vec3(0, 4, 0), vec3(0, 0, 0), rpm, thr, load, (float)k, 1.f);
                std::vector<float> b = render(1.f / 60.f);
                all.insert(all.end(), b.begin(), b.end());
            }
            destroyEmitter(h);
            std::vector<float> tail = render(0.3f);
            all.insert(all.end(), tail.begin(), tail.end());
            std::string name = std::string("engine_") + kNames[k];
            basicChecks(name, all);
            Stats s = analyze(all, 2);
            check(s.rmsDb > -40.f, "engine audible", name + StrFormat(" rms %.1f", s.rmsDb));
            if (g_report) fprintf(g_report, "%s rms %.1f dB peak %.2f\n", name.c_str(), s.rmsDb, s.peak);
            save(name, all);
        }
    }

    // ---------------------------------------------------------------- Other emitters
    {
        printf("== Emitters\n");
        struct E { EmitterType t; const char* name; float p0, p1, p2; float secs; };
        E list[] = {
            {EMIT_SIREN, "siren_wail", 0, 0, 0, 8}, {EMIT_SIREN, "siren_yelp", 1, 0, 0, 4}, {EMIT_SIREN, "siren_hilo", 2, 0, 0, 4},
            {EMIT_SIREN, "siren_ambulance", 3, 0, 0, 4}, {EMIT_HORN, "horn_car", 0.3f, 0, 0, 1.5f}, {EMIT_HORN, "horn_truck", 0.95f, 0, 0, 1.5f},
            {EMIT_TIRE_SKID, "skid_asphalt", 0.9f, 0, 0, 3}, {EMIT_TIRE_SKID, "skid_dirt", 0.9f, 1, 0, 3}, {EMIT_WIND_RUSH, "wind_rush", 45, 0, 0, 4},
            {EMIT_FIRE, "fire", 0.8f, 0, 0, 5}, {EMIT_ROTOR, "rotor", 1, 0.7f, 0, 5}, {EMIT_PROP, "prop", 0.8f, 0.8f, 0, 5},
            {EMIT_JET, "jet", 0.9f, 0.9f, 0, 5}, {EMIT_BOAT, "boat", 0.6f, 0.8f, 1, 5}, {EMIT_WATER_WAKE, "wake", 15, 0, 0, 4},
            {EMIT_ALARM, "alarm", 0, 0, 0, 15}, {EMIT_CROWD, "crowd_calm", 0.7f, 0, 0, 6}, {EMIT_CROWD, "crowd_panic", 0.9f, 1, 0, 6},
        };
        for (const E& e : list) {
            resetWorld();
            EmitterHandle h = createEmitter(e.t);
            setEmitter(h, vec3(3, 8, 0), vec3(), e.p0, e.p1, e.p2, 0, 1.f);
            std::vector<float> buf = render(e.secs);
            destroyEmitter(h);
            std::vector<float> tail = render(0.3f);
            buf.insert(buf.end(), tail.begin(), tail.end());
            basicChecks(e.name, buf);
            Stats s = analyze(buf, 2);
            check(s.rmsDb > -45.f, "emitter audible", std::string(e.name) + StrFormat(" rms %.1f", s.rmsDb));
            if (g_report) fprintf(g_report, "emitter %-16s rms %.1f dB peak %.2f\n", e.name, s.rmsDb, s.peak);
            save(std::string("emit_") + e.name, buf);
        }
        // doppler fly-by of a siren (source moving at 30 m/s past the listener)
        resetWorld();
        EmitterHandle h = createEmitter(EMIT_SIREN);
        std::vector<float> all;
        for (int s = 0; s < 8 * 60; s++) {
            float t = (float)s / 60.f;
            vec3 p(-120.f + 30.f * t, 10.f, 0.f);
            setEmitter(h, p, vec3(30.f, 0, 0), 0, 0, 0, 0, 1.f);
            std::vector<float> b = render(1.f / 60.f);
            all.insert(all.end(), b.begin(), b.end());
        }
        destroyEmitter(h);
        basicChecks("siren_flyby", all);
        save("emit_siren_flyby", all);
    }

    // ---------------------------------------------------------------- Radio stations
    {
        printf("== Radio\n");
        int nst = radioStationCount();
        check(nst >= 8, "station count");
        for (int s = 0; s < nst; s++) {
            resetWorld();
            setRadioInterior(1.f);
            setRadioStation(s);
            float secs = quick ? 20.f : 60.f;
            std::vector<float> buf = render(secs);
            std::string name = StrFormat("radio_%d", s);
            basicChecks(name, buf);
            Stats st = analyze(buf, 2);
            std::string np = radioNowPlaying(s);
            printf("  %-28s %-24s rms %.1f dB peak %.2f  now: %s\n", radioStationName(s), radioStationGenre(s), st.rmsDb, st.peak, np.c_str());
            check(st.rmsDb > -32.f && st.rmsDb < -8.f, "radio loudness", name + StrFormat(" %.1f", st.rmsDb));
            if (g_report) fprintf(g_report, "radio %d %s rms %.1f peak %.2f now '%s'\n", s, radioStationName(s), st.rmsDb, st.peak, np.c_str());
            save(name, buf);
        }
        setRadioStation(-1);
        render(0.5f);
    }

    // ---------------------------------------------------------------- Score
    {
        printf("== Score\n");
        float levels[] = {0.1f, 0.4f, 0.7f, 1.0f};
        for (float lv : levels) {
            resetWorld();
            setScore(7, lv);
            std::vector<float> buf = render(quick ? 10.f : 20.f);
            std::string name = StrFormat("score_%03d", (int)(lv * 100));
            basicChecks(name, buf);
            Stats st = analyze(buf, 2);
            printf("  intensity %.1f rms %.1f dB\n", lv, st.rmsDb);
            if (g_report) fprintf(g_report, "%s rms %.1f\n", name.c_str(), st.rmsDb);
            save(name, buf);
        }
        setScore(0, 0.f);
        render(4.f);
    }

    // ---------------------------------------------------------------- Ambience presets
    {
        printf("== Ambience\n");
        struct P { const char* name; Ambience a; };
        auto mk = [](float urban, float nature, float coast, float wet, float rain, float wind, float tod, float uw) {
            Ambience a;
            a.urban = urban; a.nature = nature; a.coast = coast; a.wetland = wet; a.rain = rain; a.wind = wind; a.timeOfDay = tod; a.underwater = uw;
            return a;
        };
        P list[] = {{"city_day", mk(1, 0, 0, 0, 0, 0.2f, 13, 0)}, {"city_night", mk(1, 0.1f, 0, 0, 0, 0.1f, 23, 0)},
                    {"forest_day", mk(0.05f, 1, 0, 0, 0, 0.3f, 8, 0)}, {"forest_night", mk(0, 1, 0, 0, 0, 0.1f, 1, 0)},
                    {"beach_day", mk(0.1f, 0.1f, 1, 0, 0, 0.5f, 15, 0)}, {"wetland_evening", mk(0, 0.3f, 0, 1, 0, 0.1f, 19.5f, 0)},
                    {"storm", mk(0.4f, 0, 0, 0, 1, 0.9f, 16, 0)}, {"underwater", mk(0.5f, 0, 1, 0, 0, 0.3f, 12, 1)}};
        for (const P& p : list) {
            resetWorld();
            setAmbience(p.a);
            render(2.f);  // crossfade in
            std::vector<float> buf = render(quick ? 8.f : 15.f);
            basicChecks(std::string("amb_") + p.name, buf);
            Stats st = analyze(buf, 2);
            printf("  %-16s rms %.1f dB peak %.2f\n", p.name, st.rmsDb, st.peak);
            check(st.rmsDb > -60.f && st.rmsDb < -16.f, "ambience level", std::string(p.name) + StrFormat(" %.1f", st.rmsDb));
            if (g_report) fprintf(g_report, "amb %-16s rms %.1f peak %.2f\n", p.name, st.rmsDb, st.peak);
            save(std::string("amb_") + p.name, buf);
        }
    }

    // ---------------------------------------------------------------- Speech + ducking
    {
        printf("== Speech\n");
        resetWorld();
        setRadioStation(0);
        render(3.f);
        VoiceParams v;
        SoundHandle h = speak("Hey. Get in the car, we are late for the ferry to Sol Beach.", v, 1.f);
        check(isPlaying(h), "speech handle playing");
        std::vector<float> buf = render(6.f);
        basicChecks("speech_over_radio", buf);
        save("speech_over_radio", buf);
        setRadioStation(-1);
        render(1.f);
    }

    // ---------------------------------------------------------------- Stress: many voices + limiter
    {
        printf("== Stress\n");
        resetWorld();
        Rng r(99);
        for (int i = 0; i < 120; i++) play((Sfx)r.irange(SFX_PISTOL, SFX_EXPLOSION_SMALL), vec3(r.range(-20, 20), r.range(-20, 20), 0), 1.f);
        std::vector<float> buf = render(3.f);
        basicChecks("stress", buf);
        Stats st = analyze(buf, 2);
        check(st.peak <= 0.96f, "limiter ceiling", StrFormat("peak %.3f", st.peak));
        save("stress", buf);
    }

    envTests();

    printf("== Drum kits (spectral sanity)\n");
    kitTests();

    printf("== Block-boundary clicks\n");
    {
        // moving siren with changing parameters, listener turning: exercises parameter ramps
        resetWorld();
        EmitterHandle h = createEmitter(EMIT_SIREN);
        std::vector<float> all;
        for (int f = 0; f < 4 * 60; f++) {
            float t = (float)f / 60.f;
            Listener l;
            l.forward = vec3(sinf(t), cosf(t), 0.f);
            update(l, 1.f / 60.f);
            setEmitter(h, vec3(20.f * sinf(t * 0.7f), 15.f, 0.f), vec3(10, 0, 0), 0, 0, 0, 0, 0.5f + 0.5f * sinf(t * 3.f));
            std::vector<float> b = render(1.f / 60.f);
            all.insert(all.end(), b.begin(), b.end());
        }
        destroyEmitter(h);
        boundaryClickCheck("siren_moving", all);
        // radio station switching (static crossfade)
        resetWorld();
        setRadioInterior(1.f);
        all.clear();
        for (int k = 0; k < 4; k++) {
            setRadioStation(k);
            std::vector<float> b = render(1.5f);
            all.insert(all.end(), b.begin(), b.end());
        }
        setRadioStation(-1);
        std::vector<float> b = render(0.5f);
        all.insert(all.end(), b.begin(), b.end());
        basicChecks("radio_switching", all);
        save("radio_switching", all);
        // pause / unpause and stop() of a long sound
        resetWorld();
        Ambience a;
        a.urban = 1.f;
        a.rain = 0.5f;
        setAmbience(a);
        render(1.f);
        SoundHandle sh = play(SFX_METAL_SCRAPE, vec3(0, 3, 0));
        all.clear();
        b = render(0.4f);
        all.insert(all.end(), b.begin(), b.end());
        stop(sh);
        setPaused(true);
        b = render(0.5f);
        all.insert(all.end(), b.begin(), b.end());
        setPaused(false);
        b = render(0.5f);
        all.insert(all.end(), b.begin(), b.end());
        basicChecks("stop_pause", all, 0.45f);
        check(!isPlaying(sh), "stopped handle not playing");
        save("stop_pause", all);
    }

    printf("== Performance\n");
    perfTest(quick ? 8.f : 20.f, 20, 8);
    perfTest(quick ? 8.f : 20.f, 6, 2);

    double t2 = TimeSeconds();
    printf("\n%d checks, %d failures, %.1f s\n", g_checks, g_fail, t2 - t0);
    if (g_report) {
        fprintf(g_report, "%d checks, %d failures\n", g_checks, g_fail);
        fclose(g_report);
    }
    shutdown();
    return g_fail ? 1 : 0;
}
