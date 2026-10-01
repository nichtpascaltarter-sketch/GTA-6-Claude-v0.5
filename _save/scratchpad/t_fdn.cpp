#include <chrono>
#include "audio/dsp.h"
void LogPrintf(const char*, ...) {}
void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
using namespace Audio::dsp;
int main() {
    enableFlushDenormals();
    FdnReverb r; r.init(1.25f, 5); r.setDecay(1.6f, 0.4f); r.setPreDelay(0.02f);
    int n = 48000 * 10;
    std::vector<float> inL(n), inR(n), oL(n), oR(n);
    Noise nz(3);
    for (int i = 0; i < 4800; i++) { inL[i] = nz.white(); inR[i] = nz.white(); }
    double t0 = TimeSeconds();
    for (int i = 0; i < n; i += 256) r.process(&inL[i], &inR[i], &oL[i], &oR[i], 256);
    double t1 = TimeSeconds();
    // decay check: energy in 1s windows
    printf("fdn %.1f ns/sample\n", (t1 - t0) / n * 1e9);
    for (int s = 0; s < 5; s++) { double e = 0; for (int i = s * 48000; i < (s + 1) * 48000; i++) e += oL[i] * oL[i] + oR[i] * oR[i]; printf("  sec %d: %.1f dB\n", s, 10 * log10(e / 96000 + 1e-20)); }
    double c = 0, a = 0, b = 0; for (int i = 4800; i < 48000; i++) { c += oL[i] * oR[i]; a += oL[i] * oL[i]; b += oR[i] * oR[i]; }
    printf("  L/R correlation %.2f\n", c / sqrt(a * b));
}
