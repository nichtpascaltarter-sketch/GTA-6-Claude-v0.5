// Radio mastering analysis: renders songs per genre through the station broadcast chain and prints
// loudness (BS.1770 integrated / LRA / true peak), octave-band tonal balance, stereo width per band
// and low/high band punch (envelope crest).
//   g++ -std=c++17 -O2 -I src radiolab.cpp -o radiolab -lpthread
//   radiolab [genre|-1] [songs] [seconds|0] [outdir|-] [raw]
#include <cstdarg>
#include <chrono>
#include <complex>
#include "audio/audio_all.cpp"
#include "../tests/audio/speech_stub.cpp"

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
    exit(1);
}
std::string StrFormat(const char* fmt, ...) {
    char b[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    return b;
}
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

using namespace Audio::detail;
using namespace Audio::detail::music;
static const char* kGenre[] = {"synthwave", "hiphop", "reggaeton", "house", "rock", "jazz", "country", "lofi", "talk"};

static void writeWav(const std::string& path, const float* L, const float* R, int n) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    int dataBytes = n * 4, riff = 36 + dataBytes, fmtLen = 16, sr = 48000, br = sr * 4;
    short fmt = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    for (int i = 0; i < n; i++) {
        short l = (short)Clamp((int)lrintf(L[i] * 32767.f), -32767, 32767), r = (short)Clamp((int)lrintf(R[i] * 32767.f), -32767, 32767);
        fwrite(&l, 2, 1, f); fwrite(&r, 2, 1, f);
    }
    fclose(f);
}

struct BQ {
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    double p(double x) {
        double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};
static BQ kShelf() { return BQ{1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585}; }
static BQ kRlb() { return BQ{1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621}; }

static void fft(std::vector<std::complex<double>>& a) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = -2 * M_PI / (double)len;
        std::complex<double> wl(cos(ang), sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1);
            for (size_t j = 0; j < len / 2; j++) {
                auto u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

struct Metrics {
    double lufs = -99, lra = 0, tp = -99, samplePeak = -99, rms = -99, corr = 0;
    double oct[10] = {};   // rel dB, centers 31.5..16k
    double sm[5] = {};     // side/mid dB: <120, 120-500, 500-2k, 2k-8k, >8k
    double lowPunch = 0, hiPunch = 0;
    double share[4] = {};  // <120, 120-500, 500-3200, >3200 (dB of total)
};

static double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[(size_t)Clamp((int)(p * (double)(v.size() - 1)), 0, (int)v.size() - 1)];
}

static Metrics analyze(const std::vector<float>& L, const std::vector<float>& R) {
    Metrics m;
    size_t n = L.size();
    // K-weighted mean squares per 100 ms
    BQ s1l = kShelf(), s2l = kRlb(), s1r = kShelf(), s2r = kRlb();
    const size_t hop = 4800;
    std::vector<double> ms100;
    double acc = 0;
    size_t cnt = 0;
    for (size_t i = 0; i < n; i++) {
        double l = s2l.p(s1l.p(L[i])), r = s2r.p(s1r.p(R[i]));
        acc += l * l + r * r;
        if (++cnt == hop) {
            ms100.push_back(acc / (double)hop);
            acc = 0;
            cnt = 0;
        }
    }
    auto blockL = [&](size_t i0, size_t k) {
        double s = 0;
        for (size_t j = 0; j < k; j++) s += ms100[i0 + j];
        return -0.691 + 10 * log10(s / (double)k + 1e-20);
    };
    std::vector<double> mom, st;
    for (size_t i = 0; i + 4 <= ms100.size(); i++) mom.push_back(blockL(i, 4));
    for (size_t i = 0; i + 30 <= ms100.size(); i += 1) st.push_back(blockL(i, 30));
    {
        double s = 0;
        int k = 0;
        for (double v : mom)
            if (v > -70) { s += pow(10, (v + 0.691) / 10); k++; }
        double ung = k ? -0.691 + 10 * log10(s / k) : -99;
        s = 0;
        k = 0;
        for (double v : mom)
            if (v > -70 && v > ung - 10) { s += pow(10, (v + 0.691) / 10); k++; }
        m.lufs = k ? -0.691 + 10 * log10(s / k) : -99;
        std::vector<double> sg;
        s = 0;
        k = 0;
        for (double v : st)
            if (v > -70) { s += pow(10, (v + 0.691) / 10); k++; }
        double stI = k ? -0.691 + 10 * log10(s / k) : -99;
        for (double v : st)
            if (v > -70 && v > stI - 20) sg.push_back(v);
        m.lra = pct(sg, 0.95) - pct(sg, 0.10);
    }
    // peaks
    double sp = 0, e = 0, ll = 0, rr = 0, lr = 0;
    for (size_t i = 0; i < n; i++) {
        sp = Max(sp, (double)Max(fabsf(L[i]), fabsf(R[i])));
        e += (double)L[i] * L[i] + (double)R[i] * R[i];
        ll += (double)L[i] * L[i];
        rr += (double)R[i] * R[i];
        lr += (double)L[i] * R[i];
    }
    m.samplePeak = 20 * log10(sp + 1e-12);
    m.rms = 10 * log10(e / (2.0 * (double)n) + 1e-20);
    m.corr = lr / sqrt(ll * rr + 1e-30);
    // true peak (4x, windowed sinc 16 taps per phase) evaluated where |x| is large
    double tp = sp;
    const int T = 8;
    float thr = (float)(sp * 0.6);
    for (int ch = 0; ch < 2; ch++) {
        const std::vector<float>& x = ch ? R : L;
        for (size_t i = (size_t)T; i + (size_t)T < n; i++) {
            if (fabsf(x[i]) < thr && fabsf(x[i + 1]) < thr) continue;
            for (int ph = 1; ph < 4; ph++) {
                double frac = ph / 4.0, s = 0;
                for (int k = -T + 1; k <= T; k++) {
                    double t = (double)k - frac;
                    double sinc = sin(M_PI * t) / (M_PI * t);
                    double w = 0.5 + 0.5 * cos(M_PI * t / (double)T);
                    s += x[i + (size_t)k] * sinc * w;
                }
                tp = Max(tp, fabs(s));
            }
        }
    }
    m.tp = 20 * log10(tp + 1e-12);
    // spectra
    const size_t N = 8192;
    std::vector<double> pw(N / 2 + 1, 0.0), pm(N / 2 + 1, 0.0), ps(N / 2 + 1, 0.0);
    std::vector<std::complex<double>> a(N), b(N);
    int frames = 0;
    for (size_t off = 0; off + N <= n; off += N / 2) {
        for (size_t i = 0; i < N; i++) {
            double w = 0.5 - 0.5 * cos(2 * M_PI * (double)i / (double)N);
            a[i] = (L[off + i] + R[off + i]) * 0.5 * w;
            b[i] = (L[off + i] - R[off + i]) * 0.5 * w;
        }
        fft(a);
        fft(b);
        for (size_t k = 0; k <= N / 2; k++) {
            double mm = std::norm(a[k]), ss = std::norm(b[k]);
            pm[k] += mm;
            ps[k] += ss;
            pw[k] += mm + ss;
        }
        frames++;
    }
    double tot = 0;
    for (size_t k = 1; k <= N / 2; k++) tot += pw[k];
    static const double cen[10] = {31.5, 63, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
    for (int bnd = 0; bnd < 10; bnd++) {
        double lo = cen[bnd] / sqrt(2.0), hi = cen[bnd] * sqrt(2.0), s = 0;
        for (size_t k = 1; k <= N / 2; k++) {
            double f = (double)k * 48000.0 / (double)N;
            if (f >= lo && f < hi) s += pw[k];
        }
        m.oct[bnd] = 10 * log10(s / (tot + 1e-30) + 1e-12);
    }
    static const double edges[6] = {20, 120, 500, 2000, 8000, 20000};
    for (int bnd = 0; bnd < 5; bnd++) {
        double sm = 0, ss = 0;
        for (size_t k = 1; k <= N / 2; k++) {
            double f = (double)k * 48000.0 / (double)N;
            if (f >= edges[bnd] && f < edges[bnd + 1]) { sm += pm[k]; ss += ps[k]; }
        }
        m.sm[bnd] = 10 * log10(ss / (sm + 1e-30) + 1e-12);
    }
    {
        static const double e4[5] = {1, 120, 500, 3200, 24000};
        for (int bnd = 0; bnd < 4; bnd++) {
            double s = 0;
            for (size_t k = 1; k <= N / 2; k++) {
                double f = (double)k * 48000.0 / (double)N;
                if (f >= e4[bnd] && f < e4[bnd + 1]) s += pw[k];
            }
            m.share[bnd] = 10 * log10(s / (tot + 1e-30) + 1e-12);
        }
    }
    // punch: P95 - P50 of 10 ms RMS envelope (dB) in 40-120 Hz and 2-8 kHz bands
    auto punch = [&](double f0, double f1) {
        Biquad h1, h2, l1, l2;
        h1.setHP((float)f0, 0.7071f); h2 = h1;
        l1.setLP((float)f1, 0.7071f); l2 = l1;
        std::vector<double> env;
        double s = 0;
        int c = 0;
        for (size_t i = 0; i < n; i++) {
            float x = 0.5f * (L[i] + R[i]);
            x = l2.process(l1.process(h2.process(h1.process(x))));
            s += (double)x * x;
            if (++c == 480) {
                env.push_back(10 * log10(s / 480 + 1e-14));
                s = 0;
                c = 0;
            }
        }
        std::vector<double> act;
        double mx = pct(env, 0.99);
        for (double v : env)
            if (v > mx - 45) act.push_back(v);
        return pct(act, 0.95) - pct(act, 0.5);
    };
    m.lowPunch = punch(40, 120);
    m.hiPunch = punch(2000, 8000);
    return m;
}

static double cpuNow() {
    timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void costReport(int songs) {
    for (int g = 0; g < 8; g++) {
        double tc = 0, tk = 0, tr = 0, tm = 0, audio = 0;
        for (int s = 0; s < songs; s++) {
            u32 seed = 1000u + (u32)g * 7919u + (u32)s * 104729u;
            double c0 = cpuNow();
            auto sd = composeSong((Genre)g, seed);
            double c1 = cpuNow();
            SongPlayer p;
            p.start(sd, 0);
            double c2 = cpuNow();
            int frames = Min((int)(60.f * 48000.f), (int)sd->length);
            frames -= frames % kProdBlock;
            std::vector<float> L((size_t)frames), R((size_t)frames);
            for (int i = 0; i < frames; i += kProdBlock) p.render(&L[(size_t)i], &R[(size_t)i], kProdBlock);
            double c3 = cpuNow();
            radio::StationProducer prod;
            radio::g_st[g].def = radio::g_defs[g];
            prod.station = g;
            prod.reset(g, 0.0);
            for (int i = 0; i < frames; i += kProdBlock) prod.process(&L[(size_t)i], &R[(size_t)i], kProdBlock);
            double c4 = cpuNow();
            prod.station = -1;
            tc += c1 - c0;
            tk += c2 - c1;
            tr += c3 - c2;
            tm += c4 - c3;
            audio += frames / 48000.0;
        }
        printf("%-10s compose %.1f ms  kit %.1f ms  render %.2f%%  master %.2f%% (per song avg; render/master as %% of one core)\n", kGenre[g], 1000 * tc / songs,
               1000 * tk / songs, 100 * tr / audio, 100 * tm / audio);
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "cost")) {
        pianoStartAsync();
        pianoWait();
        radio::ensureDefs();
        costReport(argc > 2 ? atoi(argv[2]) : 2);
        return 0;
    }
    int only = argc > 1 ? atoi(argv[1]) : -1;
    int songs = argc > 2 ? atoi(argv[2]) : 2;
    float secs = argc > 3 ? (float)atof(argv[3]) : 0.f;
    std::string outdir = argc > 4 ? argv[4] : "-";
    bool raw = argc > 5 && !strcmp(argv[5], "raw");
    pianoStartAsync();
    pianoWait();
    radio::ensureDefs();
    printf("%-10s %6s %5s %6s %5s %6s %5s | %5s %5s %5s %5s %5s %5s %5s %5s %5s %5s | S/M %5s %5s %5s %5s %5s | pL %4s pH %4s\n", "genre", "LUFS", "LRA",
           "TP", "PLR", "RMS", "corr", "31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k", "<120", "-500", "-2k", "-8k", ">8k", "", "");
    for (int g = 0; g < 8; g++) {
        if (only >= 0 && g != only) continue;
        std::vector<float> L, R;
        double t0 = TimeSeconds();
        double cpuChain = 0, clipN = 0, clipK = 0, agcEnd[4] = {0, 0, 0, 0};
        std::vector<double> songLufs;
        for (int s = 0; s < songs; s++) {
            u32 seed = 1000u + (u32)g * 7919u + (u32)s * 104729u;
            auto sd = composeSong((Genre)g, seed);
            int frames = secs > 0 ? Min((int)(secs * 48000.f), (int)sd->length) : (int)sd->length;
            frames -= frames % kProdBlock;
            SongPlayer p;
            p.start(sd, 0);
            radio::StationProducer prod;
            radio::g_st[g].def = radio::g_defs[g];
            prod.station = g;
            prod.reset(g, 0.0);
            size_t base = L.size();
            L.resize(base + (size_t)frames);
            R.resize(base + (size_t)frames);
            for (int i = 0; i < frames; i += kProdBlock) {
                p.render(&L[base + (size_t)i], &R[base + (size_t)i], kProdBlock);
                if (!raw) {
                    double c0 = TimeSeconds();
                    prod.process(&L[base + (size_t)i], &R[base + (size_t)i], kProdBlock);
                    cpuChain += TimeSeconds() - c0;
                }
            }
            std::vector<float> sl(L.begin() + (long)base, L.end()), sr(R.begin() + (long)base, R.end());
            // skip first 10 s (AGC settle) for per-song loudness
            Metrics ms = analyze(sl, sr);
            songLufs.push_back(ms.lufs);
            for (int b = 0; b < 4; b++) agcEnd[b] += prod.fm.agcDb[b];
            prod.station = -1;
        }
        double el = TimeSeconds() - t0;
        Metrics m = analyze(L, R);
        printf("%-10s %6.1f %5.1f %6.1f %5.1f %6.1f %5.2f |", kGenre[g], m.lufs, m.lra, m.tp, m.tp - m.lufs, m.rms, m.corr);
        for (int b = 0; b < 10; b++) printf(" %5.1f", m.oct[b]);
        printf(" | S/M");
        for (int b = 0; b < 5; b++) printf(" %5.1f", m.sm[b]);
        printf(" | pL %4.1f pH %4.1f", m.lowPunch, m.hiPunch);
        if (g < 8) {
            const float* t = master::kPresets[g].target;
            printf(" | 4b %5.1f %5.1f %5.1f %5.1f (d %+4.1f %+4.1f %+4.1f %+4.1f)", m.share[0], m.share[1], m.share[2], m.share[3], m.share[0] - t[0],
                   m.share[1] - t[1], m.share[2] - t[2], m.share[3] - t[3]);
        }
        printf("  [songs:");
        for (double v : songLufs) printf(" %.1f", v);
        printf("] %.1fs chain %.2f%% | clip %.2f%% agc %+.1f %+.1f %+.1f %+.1f\n", el, 100.0 * cpuChain / ((double)L.size() / 48000.0), 100.0 * clipK / Max(clipN, 1.0),
               agcEnd[0] / songs, agcEnd[1] / songs, agcEnd[2] / songs, agcEnd[3] / songs);
        if (outdir != "-") {
            int n = Min((int)L.size(), 48000 * 40);
            size_t off = L.size() > (size_t)(48000 * 70) ? (size_t)(48000 * 30) : 0;
            writeWav(outdir + "/radio_" + kGenre[g] + ".wav", &L[off], &R[off], Min(n, (int)(L.size() - off)));
        }
    }
}
