// Measures the loudness of every instrument preset on a single note (RMS of the first 300 ms) and
// the pitch accuracy (autocorrelation) so patch gains can be normalized. Build like music_lab.
#include <cstdarg>
#include <chrono>
#include "audio/audio_all.cpp"
#include "speech_stub.cpp"
void LogPrintf(const char*, ...) {}
void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
using namespace Audio::detail;
using namespace Audio::detail::music;

static float estimatePitch(const std::vector<float>& x, int start, int len, float fmin, float fmax) {
    int lagMin = (int)(48000.f / fmax), lagMax = (int)(48000.f / fmin);
    std::vector<double> cs((size_t)(lagMax + 2), -1.0);
    double best = -1e9;
    for (int lag = lagMin; lag <= lagMax; lag++) {
        double s = 0, e0 = 0, e1 = 0;
        for (int i = start; i < start + len; i++) {
            s += x[(size_t)i] * x[(size_t)(i + lag)];
            e0 += x[(size_t)i] * x[(size_t)i];
            e1 += x[(size_t)(i + lag)] * x[(size_t)(i + lag)];
        }
        cs[(size_t)lag] = s / sqrt(e0 * e1 + 1e-12);
        best = Max(best, cs[(size_t)lag]);
    }
    // smallest lag that is a local maximum within 3% of the global maximum (avoids subharmonics)
    int bestLag = lagMin;
    for (int lag = lagMin + 1; lag < lagMax; lag++)
        if (cs[(size_t)lag] >= best * 0.97 && cs[(size_t)lag] >= cs[(size_t)lag - 1] && cs[(size_t)lag] >= cs[(size_t)lag + 1]) { bestLag = lag; break; }
    // parabolic refinement
    auto corr = [&](int lag) {
        double s = 0, e0 = 0, e1 = 0;
        for (int i = start; i < start + len; i++) { s += x[(size_t)i] * x[(size_t)(i + lag)]; e0 += x[(size_t)i] * x[(size_t)i]; e1 += x[(size_t)(i + lag)] * x[(size_t)(i + lag)]; }
        return s / sqrt(e0 * e1 + 1e-12);
    };
    double a = corr(bestLag - 1), b = corr(bestLag), c = corr(bestLag + 1);
    double off = 0.5 * (a - c) / (a - 2 * b + c + 1e-12);
    return 48000.f / (float)(bestLag + off);
}

int main() {
    pianoStartAsync();
    pianoWait();
    struct P { const char* name; Patch p; int note; };
    std::vector<P> ps = {
        {"sawBass", pSawBass(), 40}, {"subBass", pSubBass(), 40}, {"supersaw", pSupersaw(0.3f, 0.5f, 2000.f), 60},
        {"pluckPulse", pPluck(Wave::Pulse, 650.f, 0.2f), 64}, {"pluckSaw", pPluck(Wave::Saw, 1200.f, 0.35f), 64},
        {"pluckSquare", pPluck(Wave::Square, 1400.f, 0.25f), 64}, {"sawLead", pSawLead(), 67}, {"squareLead", pSquareLead(), 67},
        {"fmBell", pFmBell(), 76}, {"fmEP", pFmEP(), 60}, {"fmBass", pFmBass(), 40}, {"mallet", pMallet(3.93f, 0.6f), 67},
        {"vibes", pVibes(), 67}, {"piano", pPiano(), 60}, {"guitarDist", pString(5.f, 0.7f, 0.18f, 7000.f), 52},
        {"guitarAcoustic", pString(2.6f, 0.78f, 0.13f, 8000.f), 52}, {"bassGuitar", pString(3.f, 0.35f, 0.22f, 2600.f), 33},
        {"upright", pString(1.8f, 0.22f, 0.3f, 1500.f), 33}, {"vox", pVox(), 67}, {"choir", pChoir(), 60}, {"strings", pStrings(), 60},
        {"brass", pBrass(), 60}, {"organ", pOrgan(), 60}, {"808", p808(), 33}, {"softLeadSine", pSoftLead(Wave::Sine), 72},
        {"softLeadTri", pSoftLead(Wave::Triangle), 72}, {"sax", pSax(), 64},
    };
    for (auto& e : ps) {
        Voice v;
        std::vector<float> ks(kKsCap, 0.f);
        NoteOnInfo ni{e.note, 100.f / 127.f, false, 0.f};
        voiceStart(v, e.p, ni, ks.data(), 12345u);
        int n = 48000;
        std::vector<float> L(n, 0.f), R(n, 0.f);
        for (int i = 0; i < n; i += 256) {
            if (i >= 40000) voiceRelease(v);
            if (v.active) voiceRender(v, e.p, &L[(size_t)i], &R[(size_t)i], Min(256, n - i));
        }
        double s = 0;
        for (int i = 0; i < 14400; i++) s += 0.5 * (L[(size_t)i] * L[(size_t)i] + R[(size_t)i] * R[(size_t)i]);
        float rms = (float)(10 * log10(s / 14400 + 1e-12));
        std::vector<float> m(n);
        for (int i = 0; i < n; i++) m[(size_t)i] = L[(size_t)i] + R[(size_t)i];
        float f = estimatePitch(m, 4800, 4096, 25.f, 2000.f);
        float want = midiToHz((float)e.note);
        float cents = 1200.f * log2f(f / want);
        printf("%-16s gain %.3f  rms300 %6.1f dB  pitch %7.2f Hz (want %7.2f, %+6.1f cents)\n", e.name, e.p.gain, rms, f, want, cents);
    }
}
