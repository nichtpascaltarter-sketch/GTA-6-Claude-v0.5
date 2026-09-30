// Native (Linux) test harness for the audio engine. Renders WAV files of every Sfx, engine sweeps,
// sirens and other emitters, a 60 s excerpt of every radio station, the score at several
// intensities and ambience presets, and runs objective checks (NaN/Inf, peak <= 1, DC offset,
// sample-to-sample discontinuities, loudness per category, spectral sanity). The radio music checks
// cover the station catalogs, the mastering chain (BS.1770 loudness match across stations, true peak,
// mono low end, stereo width) and the arrangement pass (song lengths, transitions, key changes).
//
// Build (from the repo root):
//   g++ -std=c++17 -O2 -I src tests/audio/test_audio.cpp -o /tmp/test_audio -lpthread
//   /tmp/test_audio [outdir] [--quick] [--env | --amb | --veh | --music | --perf]
// Define AUDIO_TEST_SPEECH_STUB to link a trivial speech stub instead of src/audio/speech.cpp.
#include <cstdarg>
#include <chrono>
#include <map>
#include <functional>
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

// Mono downmix of an interleaved stereo buffer (the spectral helpers below take mono signals).
static std::vector<float> monoOf(const std::vector<float>& inter) {
    std::vector<float> m(inter.size() / 2);
    for (size_t i = 0; i < m.size(); i++) m[i] = 0.5f * (inter[i * 2] + inter[i * 2 + 1]);
    return m;
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


// ---------------------------------------------------------------------------------------------
// Radio music: catalogs, the broadcast mastering chain and the arrangement pass.
// BS.1770 K-weighting at 48 kHz (pre-filter shelf + RLB high-pass).
struct KBiquad {
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double p(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
// Gated integrated loudness (LUFS) of an interleaved stereo buffer.
static double integratedLufs(const std::vector<float>& L, const std::vector<float>& R) {
    KBiquad s1[2] = {{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585},
                     {1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585}};
    KBiquad s2[2] = {{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621}, {1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621}};
    std::vector<double> ms;
    double acc = 0;
    int c = 0;
    for (size_t i = 0; i < L.size(); i++) {
        double l = s2[0].p(s1[0].p(L[i])), r = s2[1].p(s1[1].p(R[i]));
        acc += l * l + r * r;
        if (++c == 4800) { ms.push_back(acc / 4800.0); acc = 0; c = 0; }
    }
    std::vector<double> blk;
    for (size_t i = 0; i + 4 <= ms.size(); i++) blk.push_back((ms[i] + ms[i + 1] + ms[i + 2] + ms[i + 3]) * 0.25);
    auto gated = [&](double gateLufs) {
        double s = 0;
        int k = 0;
        for (double v : blk)
            if (-0.691 + 10 * log10(v + 1e-20) > gateLufs) { s += v; k++; }
        return k ? -0.691 + 10 * log10(s / k) : -99.0;
    };
    double ung = gated(-70.0);
    return gated(Max(-70.0, ung - 10.0));
}
// 4x oversampled peak (windowed sinc), dBTP.
static double truePeakDb(const std::vector<float>& L, const std::vector<float>& R) {
    double pk = 0;
    for (size_t i = 0; i < L.size(); i++) pk = Max(pk, (double)Max(fabsf(L[i]), fabsf(R[i])));
    double tp = pk;
    const int T = 8;
    for (int ch = 0; ch < 2; ch++) {
        const std::vector<float>& x = ch ? R : L;
        for (size_t i = T; i + T < x.size(); i++) {
            if (fabsf(x[i]) < pk * 0.6 && fabsf(x[i + 1]) < pk * 0.6) continue;
            for (int ph = 1; ph < 4; ph++) {
                double fr = ph / 4.0, v = 0;
                for (int k = -T + 1; k <= T; k++) {
                    double t = k - fr;
                    v += x[i + (size_t)k] * sin(M_PI * t) / (M_PI * t) * (0.5 + 0.5 * cos(M_PI * t / T));
                }
                tp = Max(tp, fabs(v));
            }
        }
    }
    return 20 * log10(tp + 1e-12);
}
// Side / mid energy (dB) after band-limiting both with 2nd-order filters to [f0, f1].
static double sideMidDb(const std::vector<float>& L, const std::vector<float>& R, float f0, float f1) {
    Biquad hm, hs, lm, ls;
    hm.setHP(f0, 0.7071f); hs = hm;
    lm.setLP(f1, 0.7071f); ls = lm;
    double em = 0, es = 0;
    for (size_t i = 0; i < L.size(); i++) {
        float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]);
        m = lm.process(hm.process(m));
        s = ls.process(hs.process(s));
        em += (double)m * m;
        es += (double)s * s;
    }
    return 10 * log10((es + 1e-20) / (em + 1e-20));
}

static void musicTests(bool quick) {
    using namespace detail;
    using namespace detail::music;
    printf("== Radio music: catalogs, mastering, arrangement\n");
    radio::ensureStations();
    for (int i = 0; i < radio::kStations; i++) {
        const radio::Station& st = radio::g_st[i];
        if (st.def.genre == Genre::Talk) continue;
        std::vector<std::string> titles, artists;
        for (const auto& si : st.catalog) {
            if (std::find(titles.begin(), titles.end(), si.title) == titles.end()) titles.push_back(si.title);
            if (std::find(artists.begin(), artists.end(), si.artist) == artists.end()) artists.push_back(si.artist);
        }
        printf("  %-28s %zu songs, %zu titles, %zu artists\n", st.def.name, st.catalog.size(), titles.size(), artists.size());
        check(st.catalog.size() >= 36, "station catalog size", st.def.name);
        check(titles.size() == st.catalog.size(), "unique song titles", st.def.name);
        check(artists.size() >= 12, "artists per station", st.def.name);
    }
    // mastering: one song per music station through its broadcast chain (from 50 s in, past the intro)
    std::vector<double> lufs;
    for (int g = 0; g < 8; g++) {
        int stIdx = -1;
        for (int i = 0; i < radio::kStations; i++)
            if ((int)radio::g_st[i].def.genre == g) stIdx = i;
        if (stIdx < 0) continue;
        u32 seed = 424242u + (u32)g * 1013u;
        auto sd = composeSong((Genre)g, seed);
        SongPlayer sp;
        sp.start(sd, (u32)(50.f * kSR));
        radio::StationProducer prod;
        prod.station = stIdx;
        prod.reset(stIdx, 0.0);
        int frames = (int)((quick ? 24.f : 45.f) * kSR);
        frames -= frames % kProdBlock;
        std::vector<float> L((size_t)frames), R((size_t)frames);
        double t0 = TimeSeconds();
        for (int i = 0; i < frames; i += kProdBlock) {
            sp.render(&L[(size_t)i], &R[(size_t)i], kProdBlock);
            prod.process(&L[(size_t)i], &R[(size_t)i], kProdBlock);
        }
        double el = TimeSeconds() - t0;
        prod.station = -1;
        // skip the first 6 s while the AGCs settle
        std::vector<float> l2(L.begin() + (long)(6 * kSR), L.end()), r2(R.begin() + (long)(6 * kSR), R.end());
        double lu = integratedLufs(l2, r2), tp = truePeakDb(l2, r2);
        double lowSM = sideMidDb(l2, r2, 25.f, 110.f), hiSM = sideMidDb(l2, r2, 2000.f, 12000.f);
        lufs.push_back(lu);
        const char* name = radio::g_st[stIdx].def.name;
        printf("  %-28s %5.1f LUFS  %5.1f dBTP  side/mid <110 Hz %5.1f dB, 2-12 kHz %5.1f dB  (song+chain %.1f%% of a core)\n", name, lu, tp, lowSM, hiSM,
               100.0 * el / ((double)frames / kSR));
        check(tp <= -0.5, "mastered true peak <= -0.5 dBTP", StrFormat("%s %.2f", name, tp));
        check(lowSM < -18.0, "mono low end (side/mid below 110 Hz)", StrFormat("%s %.1f", name, lowSM));
        check(hiSM > -20.0, "stereo width in the highs", StrFormat("%s %.1f", name, hiSM));
        std::vector<float> inter;
        inter.reserve(L.size() * 2);
        for (size_t i = 0; i < L.size(); i++) { inter.push_back(L[i]); inter.push_back(R[i]); }
        Stats s2 = analyze(inter, 2);
        check(s2.finite && fabs(s2.dc) < 0.01, "mastered output finite, no DC", StrFormat("%s finite %d dc %.4f", name, (int)s2.finite, s2.dc));
        save(StrFormat("master_station%d", stIdx), inter);
    }
    if (!lufs.empty()) {
        double lo = *std::min_element(lufs.begin(), lufs.end()), hi = *std::max_element(lufs.begin(), lufs.end());
        printf("  station loudness spread %.1f LU (%.1f .. %.1f LUFS)\n", hi - lo, lo, hi);
        check(hi - lo < 3.0, "stations within 3 LU of each other", StrFormat("%.1f LU", hi - lo));
        check(lo > -18.0 && hi < -10.0, "station loudness range", StrFormat("%.1f .. %.1f", lo, hi));
    }
    // arrangement pass and forms over several seeds per genre
    int keyChanges = 0, keyChangeSongs = 0;
    for (int g = 0; g < 8; g++) {
        int n = quick ? 8 : 16, trans = 0, hooks = 0, subs[4] = {0, 0, 0, 0}, backToBack = 0;
        float dmin = 1e9f, dmax = 0.f;
        for (int k = 0; k < n; k++) {
            u32 seed = 777u + (u32)g * 3001u + (u32)k * 7919u;
            SongPlan pl = planSong((Genre)g, seed);
            auto sd = composeSong((Genre)g, seed);
            dmin = Min(dmin, pl.durationSec());
            dmax = Max(dmax, pl.durationSec());
            subs[Clamp(pl.sub, 0, 3)]++;
            if (g == 0 || g == 2 || g == 4 || g == 6) {
                keyChangeSongs++;
                if (sd->keyShift) keyChanges++;
            }
            for (size_t i = 1; i < pl.sections.size(); i++) {
                const Section& a = pl.sections[i - 1];
                const Section& b = pl.sections[i];
                if (b.part == Part::Chorus && a.part == Part::Chorus) backToBack++;
                if (!((b.part == Part::Chorus || b.part == Part::Drop) && a.part != b.part)) continue;
                hooks++;
                u32 at = (u32)((double)b.startBar * 4.0 * 60.0 / pl.bpm * kSR);
                u32 win = (u32)(2.0 * 4.0 * 60.0 / pl.bpm * kSR);
                bool found = false;
                for (const NoteEv& e : sd->events)
                    if (sd->tracks[e.track].drums && (e.note == DK_REV_CYM || e.note == DK_RISER || e.note == DK_IMPACT) && e.start + win >= at &&
                        e.start <= at)
                        found = true;
                // a rhythm-section stop also counts as a transition: no kick in the last beat before the downbeat
                bool kickInLastBeat = false;
                u32 beat = (u32)(60.0 / pl.bpm * kSR);
                for (const NoteEv& e : sd->events)
                    if (e.track == 0 && e.note == DK_KICK && e.start + beat > at && e.start < at) kickInLastBeat = true;
                if (found || !kickInLastBeat) trans++;
            }
        }
        static const char* kG[] = {"synthwave", "hiphop", "reggaeton", "house", "rock", "jazz", "country", "lofi"};
        printf("  %-9s %2d songs %3.0f-%3.0f s, sub-styles %d/%d/%d, hooks with a transition %d/%d, chorus->chorus %d\n", kG[g], n, dmin, dmax, subs[0],
               subs[1], subs[2], trans, hooks, backToBack);
        check(dmin >= 140.f && dmax <= 250.f, "song lengths", kG[g]);
        if (g == 0 || g == 1 || g == 2 || g == 3) check(hooks == 0 || trans * 2 >= hooks, "transitions into choruses / drops", kG[g]);
        if (g == 0 || g == 1 || g == 3 || g == 4 || g == 7) check(subs[1] > 0, "sub-style variety", kG[g]);
    }
    printf("  last-chorus key changes: %d of %d pop / rock / country songs\n", keyChanges, keyChangeSongs);
    check(keyChanges > 0 && keyChanges < keyChangeSongs, "some (not all) songs modulate");
}

// ---------------------------------------------------------------------------------------------
// Footsteps and body foley: every footwear on every surface, gait, wet ground, landings, body impacts.
// Loudest 100 ms window of the K-weighted mono mix (dB): a perceptual level for short impacts, where plain RMS would
// be dominated by low thumps.
static float loudestKDb(const std::vector<float>& inter) {
    KBiquad s1 = {1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585};
    KBiquad s2 = {1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621};
    size_t n = inter.size() / 2;
    std::vector<double> e(n + 1, 0.0);
    for (size_t i = 0; i < n; i++) {
        double y = s2.p(s1.p(0.5 * (inter[i * 2] + inter[i * 2 + 1])));
        e[i + 1] = e[i] + y * y;
    }
    const size_t w = 4800;
    double best = 0.0;
    for (size_t i = 0; i + 1 < n; i += 120) {
        size_t j = Min(n, i + w);
        best = Max(best, (e[j] - e[i]) / (double)Max<size_t>(1, j - i));
    }
    return (float)(10.0 * log10(best + 1e-14));
}
static std::vector<float> renderStep(const Footstep& f, float secs = 0.6f) {
    playFootstep(f);
    std::vector<float> b = render(secs);
    render(0.8f);  // let the room tail die before the next one
    return b;
}
static void footstepTests() {
    printf("== Footsteps and foley\n");
    resetWorld();
    static const char* kSurf[FOOT_SURFACE_COUNT] = {"asphalt", "concrete", "grass", "dirt", "sand", "water", "wood", "metal", "mud"};
    static const char* kWear[FOOTWEAR_COUNT] = {"sneaker", "leather", "heel", "boot", "sandal", "bare"};
    const vec3 at(0.f, 3.f, -1.6f);  // a pedestrian's foot 3 m ahead of a standing listener
    // the legacy single-sample step at the old walk volume, as the level reference
    play(SFX_STEP_CONCRETE, at, 0.3f);
    std::vector<float> ref = render(0.6f);
    render(0.8f);
    float refDb = loudestKDb(ref);
    printf("  reference (old concrete step): %.1f dB\n", refDb);
    std::vector<float> all;
    float hf[FOOTWEAR_COUNT] = {}, lf[FOOTWEAR_COUNT] = {}, walkDb[FOOTWEAR_COUNT] = {};
    for (int s = 0; s < FOOT_SURFACE_COUNT; s++) {
        bool hard = s == FOOT_ASPHALT || s == FOOT_CONCRETE || s == FOOT_WOOD || s == FOOT_METAL;
        std::string line;
        for (int w = 0; w < FOOTWEAR_COUNT; w++) {
            if (!hard && w != FOOTWEAR_SNEAKER && w != FOOTWEAR_BARE) continue;
            float db = 0.f, h = 0.f, l = 0.f;
            const int reps = 3;
            for (int k = 0; k < reps; k++) {
                Footstep f;
                f.pos = at;
                f.speed = 1.4f;
                f.surface = (u8)s;
                f.footwear = (u8)w;
                std::vector<float> b = renderStep(f);
                basicChecks(StrFormat("step_%s_%s", kSurf[s], kWear[w]), b, 1.2f);
                db += loudestKDb(b) / reps;
                std::vector<float> m = monoOf(b);
                h += bandFraction(m, 2000.f, 20000.f) / reps;
                l += bandFraction(m, 20.f, 300.f) / reps;
                if (k == 0) all.insert(all.end(), b.begin(), b.end());
            }
            line += StrFormat(" %s %.1f", kWear[w], db);
            check(db > refDb - 8.f && db < refDb + 8.f, "footstep level near the reference", StrFormat("%s/%s %.1f dB", kSurf[s], kWear[w], db));
            if (s == FOOT_CONCRETE) { hf[w] = h; lf[w] = l; walkDb[w] = db; }
        }
        printf("  %-8s%s dB\n", kSurf[s], line.c_str());
    }
    save("steps_matrix", all);
    printf("  concrete >2 kHz: sneaker %.0f%% leather %.0f%% heel %.0f%% boot %.0f%%;  <300 Hz: sneaker %.0f%% boot %.0f%%\n", hf[0] * 100,
           hf[1] * 100, hf[2] * 100, hf[3] * 100, lf[0] * 100, lf[3] * 100);
    check(hf[FOOTWEAR_HEEL] > hf[FOOTWEAR_SNEAKER] + 0.1f, "heels click brighter than sneakers");
    check(hf[FOOTWEAR_LEATHER] > hf[FOOTWEAR_SNEAKER], "leather soles brighter than sneakers");
    check(lf[FOOTWEAR_BOOT] > lf[FOOTWEAR_HEEL], "boots heavier than heels");
    // gait: a sprinting step lands harder than a stroll
    auto stepDb = [&](float speed, int surf, float wet, int ev, float impact) {
        float db = 0.f;
        for (int k = 0; k < 4; k++) {
            Footstep f;
            f.pos = at;
            f.speed = speed;
            f.surface = (u8)surf;
            f.wetness = wet;
            f.event = (u8)ev;
            f.impact = impact;
            db += loudestKDb(renderStep(f)) / 4.f;
        }
        return db;
    };
    float walk = stepDb(1.4f, FOOT_ASPHALT, 0.f, FOOT_STEP, 0.f), sprint = stepDb(7.f, FOOT_ASPHALT, 0.f, FOOT_STEP, 0.f);
    float wetDb = stepDb(1.4f, FOOT_ASPHALT, 1.f, FOOT_STEP, 0.f);
    float land = stepDb(1.f, FOOT_CONCRETE, 0.f, FOOT_LAND, 6.f);
    printf("  asphalt: walk %.1f dB, sprint %.1f dB, walk on wet ground %.1f dB, landing from a jump %.1f dB\n", walk, sprint, wetDb, land);
    check(sprint > walk + 3.f, "sprint steps land harder than a stroll");
    check(wetDb > walk + 0.5f, "wet ground adds splashes");
    check(land > walk + 4.f, "a landing is heavier than a step");
    // body impacts
    playBodyImpact(at, 5.f, FOOT_CONCRETE, true);
    std::vector<float> body = render(1.2f);
    render(1.f);
    basicChecks("body_thud_concrete", body, 1.2f);
    float bodyDb = loudestKDb(body), bodyLf = bandFraction(monoOf(body), 20.f, 250.f);
    printf("  body falling on concrete at 5 m/s: %.1f dB, %.0f%% below 250 Hz\n", bodyDb, bodyLf * 100);
    check(bodyDb > walk + 6.f, "a falling body is much louder than a step");
    check(bodyLf > 0.3f, "body thud is heavy");
    save("body_thud_concrete", body);
    for (int s : {FOOT_GRASS, FOOT_WOOD, FOOT_METAL, FOOT_WATER}) {
        playBodyImpact(at, 4.f, (u8)s, true);
        std::vector<float> b = render(1.f);
        render(1.f);
        basicChecks(StrFormat("body_thud_%s", kSurf[s]), b, 1.2f);
        check(loudestKDb(b) > walk, "body impact audible", kSurf[s]);
        save(StrFormat("body_thud_%s", kSurf[s]), b);
    }
    for (int k : {FOLEY_CLOTH, FOLEY_GEAR, FOLEY_GRAB}) {
        playFoley(vec3(0.f, 1.f, -0.5f), (u8)k, 1.f);
        std::vector<float> b = render(0.8f);
        render(0.5f);
        basicChecks(StrFormat("foley_%d", k), b, 1.2f);
        check(loudestKDb(b) > -60.f, "foley audible", StrFormat("%d", k));
    }
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
enum VScene { VS_OPEN = 0, VS_STREET, VS_ROOM, VS_TUNNEL, VS_STATION, VS_COUNT, VS_BUILDING = VS_COUNT, VS_LOWWALL, VS_AWNING };
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
        case VS_AWNING:  // a shop awning 4 m up in front of a facade
            g_vboxes.push_back({vec3(-4, -4, 4), vec3(4, 4, 4.2f)});
            g_vboxes.push_back({vec3(-30, -5, 0), vec3(30, -4.2f, 20)});
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
    float hiNear = bandFraction(monoOf(std::vector<float>(npc.begin(), npc.begin() + Min(npc.size(), (size_t)96000))), 2000.f, 20000.f);
    std::vector<float> distWin(dist.begin() + (size_t)(1.0f * 48000.f) * 2, dist.begin() + (size_t)(2.5f * 48000.f) * 2);
    float hiFar = bandFraction(monoOf(distWin), 2000.f, 20000.f);
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
            hi[k] = bandFraction(monoOf(eng), 1500.f, 20000.f);
            playGunshot(SFX_RIFLE, vec3(0, 40, 1.5f), vec3(1, 0, 0), 0);
            std::vector<float> g = renderScene(2.f, 0.f);
            gun[k] = loudestWindowDb(g, 2, 0.1f);
            gunHi[k] = bandFraction(monoOf(std::vector<float>(g.begin() + 12000, g.begin() + 12000 + 48000)), 2000.f, 20000.f);
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

// Vehicles: tyres by surface, wet spray, limiter rhythm, downshift blip, thumps, cabin relay, intake/exhaust balance.
static std::vector<float> driveScene(int kind, float secs, const std::function<void(float, VehicleAudio&, vec3&, float&, float&, float&)>& fn,
                                     Listener l) {
    EmitterHandle h = createEmitter(EMIT_ENGINE);
    std::vector<float> all;
    int frames = (int)(secs * 60.f);
    for (int f = 0; f < frames; f++) {
        float t = (float)f / 60.f;
        VehicleAudio va;
        vec3 pos(0, 6, 0.5f);
        float rpm = 0.4f, thr = 0.5f, load = 0.5f;
        fn(t, va, pos, rpm, thr, load);
        update(l, 1.f / 60.f);
        setEmitter(h, pos, va.forward * va.speed, rpm, thr, load, (float)kind, 1.f);
        setVehicleAudio(h, va);
        std::vector<float> b = render(1.f / 60.f);
        all.insert(all.end(), b.begin(), b.end());
    }
    destroyEmitter(h);
    render(0.3f);
    return all;
}
// Rhythm of an amplitude envelope (5 ms RMS, 1 ms hop): the autocorrelation peak between 1/f1 and 1/f0, as a frequency.
static float envelopePeakHz(const std::vector<float>& x, float f0, float f1) {
    std::vector<float> env;  // 5 ms windows every 1 ms
    for (size_t i = 0; i + 240 <= x.size(); i += 48) {
        double e = 0;
        for (size_t k = i; k < i + 240; k++) e += (double)x[k] * x[k];
        env.push_back(20.f * log10f((float)sqrt(e / 240.0) + 1e-5f));  // dB: every event counts alike
    }
    float mean = 0.f;
    for (float v : env) mean += v;
    mean /= (float)Max((size_t)1, env.size());
    for (float& v : env) v -= mean;
    int l0 = Max(1, (int)(1000.f / f1)), l1 = (int)(1000.f / f0);
    std::vector<float> ac;
    float best = -1e30f;
    for (int L = l0; L <= l1 && L < (int)env.size() / 2; L++) {
        double c = 0;
        for (size_t i = 0; i + (size_t)L < env.size(); i++) c += (double)env[i] * env[i + (size_t)L];
        c /= (double)(env.size() - (size_t)L);
        ac.push_back((float)c);
        best = Max(best, (float)c);
    }
    // the shortest period whose autocorrelation is (nearly) as strong as the best: avoids octave errors
    for (size_t k = 1; k + 1 < ac.size(); k++)
        if (ac[k] >= 0.85f * best && ac[k] >= ac[k - 1] && ac[k] >= ac[k + 1]) return 1000.f / (float)(l0 + (int)k);
    return 0.f;
}
static void vehicleTests() {
    printf("== Vehicles\n");
    Listener l0;
    l0.pos = vec3(0, 0, 1.6f);
    // tyres by surface: a car rolling past at 20 m/s (engine quiet, cruising)
    const char* sname[5] = {"asphalt", "wet", "gravel", "grass", "wood"};
    float lvl[5], hi[5];
    for (int k = 0; k < 5; k++) {
        resetWorld();
        std::vector<float> b = driveScene(ENGINE_I4, 4.f, [&](float t, VehicleAudio& va, vec3& pos, float& rpm, float& thr, float& load) {
            va.speed = 20.f;
            va.forward = vec3(1, 0, 0);
            va.surface = k == 2 ? 3 : k == 3 ? 2 : k == 4 ? 6 : 0;
            va.gravel = k == 2;
            va.wetness = k == 1 ? 1.f : 0.f;
            pos = vec3(-40.f + 20.f * t, 6.f, 0.5f);
            rpm = 0.35f; thr = 0.25f; load = 0.2f;
        }, l0);
        basicChecks(std::string("veh_surface_") + sname[k], b);
        save(std::string("veh_surface_") + sname[k], b);
        std::vector<float> m = monoOf(b);
        lvl[k] = analyze(b, 2).rmsDb;
        hi[k] = bandFraction(m, 3000.f, 16000.f);
    }
    printf("  surfaces (asphalt / wet / gravel / grass / wood): rms %.1f / %.1f / %.1f / %.1f / %.1f dB, >3 kHz %.1f / %.1f / %.1f / %.1f / %.1f %%\n",
           lvl[0], lvl[1], lvl[2], lvl[3], lvl[4], hi[0] * 100.f, hi[1] * 100.f, hi[2] * 100.f, hi[3] * 100.f, hi[4] * 100.f);
    check(hi[1] > hi[0] * 1.5f, "wet road: spray hiss");
    check(hi[2] > hi[0] * 1.5f, "gravel: crunch and stones");
    // no chassis state vs chassis at speed: the tyres add a lot at 25 m/s
    {
        resetWorld();
        auto run = [&](bool chassis) {
            EmitterHandle h = createEmitter(EMIT_ENGINE);
            std::vector<float> all;
            for (int f = 0; f < 150; f++) {
                update(l0, 1.f / 60.f);
                setEmitter(h, vec3(0, 8, 0.5f), vec3(25, 0, 0), 0.4f, 0.4f, 0.4f, (float)ENGINE_V6, 1.f);
                if (chassis) {
                    VehicleAudio va;
                    va.speed = 25.f;
                    va.forward = vec3(1, 0, 0);
                    setVehicleAudio(h, va);
                }
                std::vector<float> b = render(1.f / 60.f);
                if (f > 60) all.insert(all.end(), b.begin(), b.end());
            }
            destroyEmitter(h);
            render(0.3f);
            return analyze(all, 2).rmsDb;
        };
        float e0 = run(false), e1 = run(true);
        printf("  V6 at 25 m/s: engine only %.1f dB, with tyres and wind %.1f dB\n", e0, e1);
        check(e1 > e0 + 1.5f, "tyre roar at speed");
    }
    // rev limiter: bouncing at ~16 Hz
    {
        resetWorld();
        std::vector<float> b = driveScene(ENGINE_V8, 3.f, [&](float t, VehicleAudio& va, vec3& pos, float& rpm, float& thr, float& load) {
            (void)t;
            va.speed = 0.f;
            rpm = 1.f; thr = 1.f; load = 0.3f;
            pos = vec3(0, 5, 0.5f);
        }, l0);
        std::vector<float> m = monoOf(std::vector<float>(b.begin() + 48000, b.end()));
        float fz = envelopePeakHz(m, 6.f, 40.f);
        printf("  rev limiter envelope modulation %.1f Hz\n", fz);
        check(fz > 12.f && fz < 20.f, "rev limiter bounces at ~16 Hz", StrFormat("%.1f", fz));
        save("veh_rev_limiter", b);
    }
    // downshift blip: throttle off, gear 3 -> 2 at t = 1 s
    {
        resetWorld();
        std::vector<float> b = driveScene(ENGINE_V8, 2.f, [&](float t, VehicleAudio& va, vec3& pos, float& rpm, float& thr, float& load) {
            va.speed = 15.f;
            va.gear = t < 1.f ? 3 : 2;
            rpm = t < 1.f ? 0.35f : 0.55f;
            thr = 0.f; load = 0.1f;
            pos = vec3(0, 5, 0.5f);
        }, l0);
        float before = windowDb(b, 0.8f, 0.95f), after = windowDb(b, 1.0f, 1.12f);
        printf("  downshift: %.1f dB before, %.1f dB during the blip\n", before, after);
        check(after > before + 3.f, "rev-matching blip on a downshift");
        save("veh_downshift_blip", b);
    }
    // suspension thump over a kerb
    {
        resetWorld();
        std::vector<float> b = driveScene(ENGINE_I4, 2.f, [&](float t, VehicleAudio& va, vec3& pos, float& rpm, float& thr, float& load) {
            va.speed = 8.f;
            va.bump = (t > 1.f && t < 1.02f) ? 2.5f : 0.f;
            rpm = 0.2f; thr = 0.2f; load = 0.2f;
            pos = vec3(0, 5, 0.5f);
        }, l0);
        std::vector<float> seg(b.begin() + 96000, b.begin() + 96000 + 24000);
        std::vector<float> ref(b.begin() + 72000, b.begin() + 72000 + 24000);
        float lowHit = bandFraction(monoOf(seg), 30.f, 200.f), lowRef = bandFraction(monoOf(ref), 30.f, 200.f);
        float eHit = windowDb(b, 1.0f, 1.25f), eRef = windowDb(b, 0.75f, 1.0f);
        printf("  kerb thump: %.1f dB (<200 Hz %.0f%%) vs %.1f dB (<200 Hz %.0f%%) before\n", eHit, lowHit * 100.f, eRef, lowRef * 100.f);
        check(eHit > eRef + 2.f && lowHit > lowRef, "suspension thump over a kerb");
        save("veh_kerb_thump", b);
    }
    // cabin: the player's car with the indicator on, listener inside
    {
        resetWorld();
        Listener in = l0;
        in.inVehicle = 1.f;
        in.interior = 0.6f;
        in.pos = vec3(0, 5.2f, 1.2f);
        std::vector<float> b = driveScene(ENGINE_I4, 3.f, [&](float t, VehicleAudio& va, vec3& pos, float& rpm, float& thr, float& load) {
            (void)t;
            va.speed = 0.f;
            va.player = true;
            va.indicator = 1;
            rpm = 0.f; thr = 0.f; load = 0.05f;
            pos = vec3(0, 5, 0.6f);
        }, in);
        std::vector<float> m = monoOf(b);
        std::vector<float> hp(m.size());
        OnePoleHP f;
        f.set(1500.f);
        for (size_t i = 0; i < m.size(); i++) hp[i] = f.process(m[i]);
        float fz = envelopePeakHz(hp, 1.f, 8.f);
        printf("  cabin relay rhythm %.2f Hz (tick-tock every 0.36 s)\n", fz);
        check(fz > 2.5f && fz < 3.1f, "indicator relay ticks in the cabin", StrFormat("%.2f", fz));
        save("veh_cabin_indicator", b);
    }
    // intake / exhaust balance: listener in front of the car vs behind it
    {
        float lowF = 0, lowB = 0, midF = 0, midB = 0;
        for (int side = 0; side < 2; side++) {
            resetWorld();
            std::vector<float> b = driveScene(ENGINE_V8, 2.5f, [&](float t, VehicleAudio& va, vec3& pos, float& rpm, float& thr, float& load) {
                (void)t;
                va.speed = 0.f;
                va.forward = side == 0 ? vec3(0, -1, 0) : vec3(0, 1, 0);  // facing the listener / facing away
                rpm = 0.55f; thr = 0.8f; load = 0.6f;
                pos = vec3(0, 6, 0.5f);
            }, l0);
            std::vector<float> m = monoOf(std::vector<float>(b.begin() + 48000, b.end()));
            (side == 0 ? lowF : lowB) = bandFraction(m, 30.f, 300.f);
            (side == 0 ? midF : midB) = bandFraction(m, 800.f, 4000.f);
            save(side == 0 ? "veh_v8_front" : "veh_v8_behind", b);
        }
        printf("  V8 heard from the front / behind: <300 Hz %.0f%% / %.0f%%, 0.8-4 kHz %.0f%% / %.0f%%\n", lowF * 100.f, lowB * 100.f, midF * 100.f,
               midB * 100.f);
        check(lowB > lowF && midF > midB, "exhaust behind, intake and engine bay in front");
    }
    // air brakes when a bus stops
    {
        resetWorld();
        std::vector<float> b = driveScene(ENGINE_TRUCK_DIESEL, 3.f, [&](float t, VehicleAudio& va, vec3& pos, float& rpm, float& thr, float& load) {
            va.speed = Max(0.f, 6.f - 4.f * t);
            va.heavy = true;
            rpm = 0.1f; thr = 0.f; load = 0.1f;
            pos = vec3(0, 7, 0.5f);
        }, l0);
        float hiStop = windowDb(b, 1.55f, 2.2f) + 10.f * log10f(bandFraction(monoOf(std::vector<float>(b.begin() + 148800, b.begin() + 211200)), 2000.f, 12000.f) + 1e-9f);
        float hiBefore = windowDb(b, 0.6f, 1.3f) + 10.f * log10f(bandFraction(monoOf(std::vector<float>(b.begin() + 57600, b.begin() + 124800)), 2000.f, 12000.f) + 1e-9f);
        printf("  bus stopping: 2-12 kHz energy %.1f dB rolling, %.1f dB after the stop (air brakes)\n", hiBefore, hiStop);
        check(hiStop > hiBefore + 6.f, "air brakes vent when a bus stops");
        save("veh_bus_air_brakes", b);
    }
}

// Ambience presets by district, time and shelter, with spectral sanity checks.
static void ambienceTests(bool quick) {
    printf("== Ambience\n");
    struct P {
        const char* name;
        Ambience a;
        int scene;        // virtual geometry around the listener (VS_OPEN = none)
        float interior, inVehicle;
    };
    auto mk = [](float urban, float nature, float coast, float wet, float rain, float wind, float tod, float uw, float down = 0.f, float port = 0.f,
                 float traffic = -1.f) {
        Ambience a;
        a.urban = urban; a.nature = nature; a.coast = coast; a.wetland = wet; a.rain = rain; a.wind = wind; a.timeOfDay = tod; a.underwater = uw;
        a.downtown = down; a.port = port; a.traffic = traffic;
        return a;
    };
    P list[] = {
        {"city_day", mk(1, 0, 0, 0, 0, 0.2f, 13, 0, 0, 0, 0.7f), VS_OPEN, 0, 0},
        {"city_night", mk(1, 0.1f, 0, 0, 0, 0.1f, 23, 0, 0, 0, 0.3f), VS_OPEN, 0, 0},
        {"downtown_day", mk(1, 0, 0, 0, 0, 0.2f, 11, 0, 1, 0, 0.9f), VS_STREET, 0, 0},
        {"downtown_night", mk(1, 0, 0, 0, 0, 0.1f, 2, 0, 1, 0, 0.35f), VS_STREET, 0, 0},
        {"port_day", mk(0.5f, 0, 0.4f, 0, 0, 0.4f, 10, 0, 0, 1, 0.3f), VS_OPEN, 0, 0},
        {"forest_day", mk(0.05f, 1, 0, 0, 0, 0.3f, 8, 0), VS_OPEN, 0, 0},
        {"forest_night", mk(0, 1, 0, 0, 0, 0.1f, 1, 0), VS_OPEN, 0, 0},
        {"beach_day", mk(0.1f, 0.1f, 1, 0, 0, 0.5f, 15, 0), VS_OPEN, 0, 0},
        {"sawgrass_day", mk(0, 0.4f, 0, 1, 0, 0.3f, 11, 0), VS_OPEN, 0, 0},
        {"sawgrass_night", mk(0, 0.4f, 0, 1, 0, 0.1f, 23, 0), VS_OPEN, 0, 0},
        {"wetland_evening", mk(0, 0.3f, 0, 1, 0, 0.1f, 19.5f, 0), VS_OPEN, 0, 0},
        {"storm", mk(0.4f, 0, 0, 0, 1, 0.9f, 16, 0), VS_OPEN, 0, 0},
        {"rain_open", mk(0.8f, 0, 0, 0, 0.8f, 0.3f, 16, 0), VS_OPEN, 0, 0},
        {"rain_sheltered", mk(0.8f, 0, 0, 0, 0.8f, 0.3f, 16, 0), VS_AWNING, 0, 0},
        {"rain_in_car", mk(0.8f, 0, 0, 0, 0.8f, 0.3f, 16, 0), VS_OPEN, 0.6f, 1},
        {"rain_indoors", mk(0.8f, 0, 0, 0, 0.8f, 0.3f, 16, 0), VS_ROOM, 1, 0},
        {"underwater", mk(0.5f, 0, 1, 0, 0, 0.3f, 12, 1), VS_OPEN, 0, 0},
    };
    const int N = (int)(sizeof(list) / sizeof(list[0]));
    std::map<std::string, float> rms, lo, hi, mid, ins;
    for (int k = 0; k < N; k++) {
        const P& p = list[k];
        resetWorld();
        setScene(p.scene);
        setRaycast(p.scene != VS_OPEN ? vRay : nullptr);
        setAmbience(p.a);
        Listener l;
        l.pos = vec3(0, 0, 1.7f);
        l.interior = p.interior;
        l.inVehicle = p.inVehicle;
        std::vector<float> buf;
        int frames = (int)((quick ? 10.f : 17.f) * 60.f);
        for (int f = 0; f < frames; f++) {
            update(l, 1.f / 60.f);
            std::vector<float> b = render(1.f / 60.f);
            if (f >= 120) buf.insert(buf.end(), b.begin(), b.end());  // after the crossfade in
        }
        basicChecks(std::string("amb_") + p.name, buf);
        Stats st = analyze(buf, 2);
        rms[p.name] = st.rmsDb;
        std::vector<float> mb = monoOf(buf);
        lo[p.name] = bandFraction(mb, 20.f, 160.f);
        mid[p.name] = bandFraction(mb, 500.f, 1600.f);
        ins[p.name] = bandFraction(mb, 2200.f, 10000.f);
        hi[p.name] = bandFraction(mb, 4000.f, 20000.f);
        printf("  %-16s rms %.1f dB peak %.2f  <160 Hz %2.0f%%  0.5-1.6k %2.0f%%  2.2-10k %2.0f%%  >4k %2.0f%%\n", p.name, st.rmsDb, st.peak, lo[p.name] * 100.f,
               mid[p.name] * 100.f, ins[p.name] * 100.f, hi[p.name] * 100.f);
        check(st.rmsDb > -62.f && st.rmsDb < -16.f, "ambience level", std::string(p.name) + StrFormat(" %.1f", st.rmsDb));
        if (g_report) fprintf(g_report, "amb %-16s rms %.1f peak %.2f\n", p.name, st.rmsDb, st.peak);
        save(std::string("amb_") + p.name, buf);
    }
    setRaycast(nullptr);
    setScene(VS_OPEN);
    check(ins["sawgrass_night"] > ins["city_night"] + 0.05f, "night insects in the sawgrass");
    check(ins["forest_night"] > ins["city_night"], "crickets and katydids at night in the woods");
    check(lo["port_day"] > lo["beach_day"], "port generator drone");
    check(rms["downtown_day"] > rms["city_night"], "downtown by day busier than the city at night");
    auto bandDb = [&](const char* nm, std::map<std::string, float>& band) { return rms[nm] + 10.f * log10f(band[nm] + 1e-9f); };
    check(bandDb("rain_in_car", hi) < bandDb("rain_open", hi) - 5.f, "rain heard from the car is muffled",
          StrFormat("%.1f vs %.1f dB above 4 kHz", bandDb("rain_in_car", hi), bandDb("rain_open", hi)));
    check(rms["rain_indoors"] < rms["rain_open"] - 8.f && hi["rain_indoors"] < hi["rain_open"] * 0.4f, "rain heard indoors is muffled");
    check(mid["rain_sheltered"] > mid["rain_open"], "rain drums on the awning overhead");
    // thunder by distance
    float tl[3], th[3];
    const float dists[3] = {500.f, 2000.f, 6000.f};
    for (int k = 0; k < 3; k++) {
        resetWorld();
        playThunder(dists[k], 1.f);
        std::vector<float> b = render(8.f);
        basicChecks(StrFormat("thunder_%.0fm", dists[k]), b, 1.6f);
        tl[k] = loudestWindowDb(b, 2);
        th[k] = bandFraction(monoOf(b), 1000.f, 20000.f);
        save(StrFormat("thunder_%.0fm", dists[k]), b);
    }
    printf("  thunder 500 / 2000 / 6000 m: loud %.1f / %.1f / %.1f dB, >1 kHz %.1f / %.1f / %.1f %%\n", tl[0], tl[1], tl[2], th[0] * 100.f,
           th[1] * 100.f, th[2] * 100.f);
    check(tl[0] > tl[2] + 3.f && th[0] > th[1] && th[1] >= th[2], "close thunder cracks, far thunder rumbles");
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
                if ((int)i < engines) {
                    VehicleAudio va;
                    va.forward = vec3(1, 0, 0);
                    va.speed = 8.f + 10.f * (float)(i % 3);
                    va.surface = (int)(i % 4) == 3 ? 3 : 0;
                    va.wetness = 0.6f;
                    va.slip = (i % 7) == 0 ? 0.6f : 0.05f;
                    setVehicleAudio(em[i], va);
                }
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

// Busy sidewalk on foot: the player running through a crowd of 24 walkers (their footsteps by surface and footwear),
// traffic passing, rain on the ground, the occasional body falling.
static void perfFootTest(float seconds) {
    resetWorld();
    dsp::ScopedFlushDenormals ftz;
    Ambience a;
    a.urban = 1.f; a.rain = 0.4f; a.timeOfDay = 18.f;
    setAmbience(a);
    setScene(VS_STREET);
    setRaycast(vRay);
    std::vector<EmitterHandle> em;
    for (int i = 0; i < 6; i++) em.push_back(createEmitter(EMIT_ENGINE));
    render(1.f);
    Rng r(11);
    struct Walker { vec3 pos, vel; float phase, stride; u8 wear, surf; };
    std::vector<Walker> walkers;
    for (int i = 0; i < 24; i++) {
        float ang = r.range(0.f, kTwoPi), d = r.range(3.f, 28.f);
        walkers.push_back({vec3(cosf(ang) * d, sinf(ang) * d, -1.6f), vec3(r.range(-1.4f, 1.4f), r.range(-1.4f, 1.4f), 0.f), r.f(), 0.75f,
                           (u8)r.irange(0, FOOTWEAR_COUNT - 1), (u8)(r.chance(0.8f) ? FOOT_CONCRETE : FOOT_ASPHALT)});
    }
    g_cpuClock = true;
    double tMix = 0;
    int blocks = (int)(seconds * 48000.f / 256.f), steps = 0;
    float out[512], playerPhase = 0.f;
    for (int b = 0; b < blocks; b++) {
        if (b % 3 == 0) {
            float dt = 1.f / 62.5f, t = (float)b * 256.f / 48000.f;
            Listener l;
            l.pos = vec3(0, t * 5.f, 0);
            l.vel = vec3(0, 5.f, 0);
            update(l, dt);
            for (size_t i = 0; i < em.size(); i++)
                setEmitter(em[i], l.pos + vec3(12.f + 4.f * (float)i, 30.f * sinf(t * 0.3f + (float)i), 0), vec3(0, 12, 0), 0.4f, 0.4f, 0.3f,
                           (float)(i % ENGINE_COUNT), 1.f);
            for (Walker& w : walkers) {
                w.pos += w.vel * dt;
                w.phase += length(w.vel) * dt / w.stride;
                if (w.phase >= 1.f) {
                    w.phase -= 1.f;
                    Footstep f;
                    f.pos = l.pos + w.pos;
                    f.speed = length(w.vel);
                    f.footwear = w.wear;
                    f.surface = w.surf;
                    f.wetness = 0.5f;
                    playFootstep(f);
                    steps++;
                }
            }
            playerPhase += 5.f * dt / 1.3f;
            if (playerPhase >= 1.f) {
                playerPhase -= 1.f;
                Footstep f;
                f.pos = l.pos + vec3(0.f, 0.f, -1.6f);
                f.speed = 5.f;
                f.wetness = 0.5f;
                f.player = true;
                playFootstep(f);
            }
            if (r.chance(0.01f)) playBodyImpact(l.pos + vec3(r.range(-10, 10), r.range(3, 15), -1.5f), 5.f, FOOT_CONCRETE, true);
        }
        double t1 = TimeSeconds();
        detail::mix::g_mixer->render(out, 256);
        tMix += TimeSeconds() - t1;
    }
    g_cpuClock = false;
    for (auto h : em) destroyEmitter(h);
    setRaycast(nullptr);
    setScene(VS_OPEN);
    printf("  perf on foot (24 walkers, %.0f steps/s, 6 engines, rain): mixer %.2f%% of one core\n", (float)steps / seconds, 100.0 * tMix / seconds);
    if (g_report) fprintf(g_report, "perf foot mixer %.2f%%\n", 100.0 * tMix / seconds);
    check(tMix / seconds < 0.10, "mixer cpu on a crowded sidewalk < 10% of a core");
}

int main(int argc, char** argv) {
    bool quick = false, perfOnly = false, envOnly = false, ambOnly = false, vehOnly = false, musicOnly = false, footOnly = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--quick")) quick = true;
        else if (!strcmp(argv[i], "--music")) musicOnly = true;
        else if (!strcmp(argv[i], "--foot")) footOnly = true;
        else if (!strcmp(argv[i], "--perf")) perfOnly = true;
        else if (!strcmp(argv[i], "--env")) envOnly = true;
        else if (!strcmp(argv[i], "--amb")) ambOnly = true;
        else if (!strcmp(argv[i], "--veh")) vehOnly = true;
        else g_out = argv[i];
    }
    if (envOnly || ambOnly || vehOnly || musicOnly || footOnly) {
        mkdir(g_out.c_str(), 0755);
        init();
        render(0.1f);
        resetWorld();
        if (envOnly) envTests();
        if (ambOnly) ambienceTests(quick);
        if (vehOnly) vehicleTests();
        if (musicOnly) musicTests(quick);
        if (footOnly) footstepTests();
        printf("\n%d checks, %d failures\n", g_checks, g_fail);
        shutdown();
        return g_fail ? 1 : 0;
    }
    if (perfOnly) {
        init();
        render(0.1f);
        float secs = getenv("AUDIO_PERF_SECONDS") ? (float)atof(getenv("AUDIO_PERF_SECONDS")) : 20.f;
        perfTest(secs, 20, 8);
        if (!getenv("AUDIO_PERF_SECONDS")) perfTest(secs, 6, 2);
        perfFootTest(secs);
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
            basicChecks(name, buf, 1.25f);  // mastered music has steep transients (block-boundary clicks are tested separately)
            Stats st = analyze(buf, 2);
            std::string np = radioNowPlaying(s);
            printf("  %-28s %-24s rms %.1f dB peak %.2f  now: %s\n", radioStationName(s), radioStationGenre(s), st.rmsDb, st.peak, np.c_str());
            check(st.rmsDb > -32.f && st.rmsDb < -8.f, "radio loudness", name + StrFormat(" %.1f", st.rmsDb));
            if (g_report) fprintf(g_report, "radio %d %s rms %.1f peak %.2f now '%s'\n", s, radioStationName(s), st.rmsDb, st.peak, np.c_str());
            save(name, buf);
        }
        setRadioStation(-1);
        render(0.5f);
        musicTests(quick);
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
    ambienceTests(quick);

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
    vehicleTests();
    footstepTests();

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
    perfFootTest(quick ? 8.f : 20.f);

    double t2 = TimeSeconds();
    printf("\n%d checks, %d failures, %.1f s\n", g_checks, g_fail, t2 - t0);
    if (g_report) {
        fprintf(g_report, "%d checks, %d failures\n", g_checks, g_fail);
        fclose(g_report);
    }
    shutdown();
    return g_fail ? 1 : 0;
}
