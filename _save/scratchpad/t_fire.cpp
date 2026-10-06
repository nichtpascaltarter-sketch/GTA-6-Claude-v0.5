#include <cstdarg>
#include <chrono>
#include "audio/audio_internal.h"
#include "audio/emitters.cpp"
void LogPrintf(const char*, ...) {}
void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
using namespace Audio::detail;
int main() {
    for (int ftz = 0; ftz < 2; ftz++) {
        if (ftz) Audio::dsp::enableFlushDenormals();
        alignas(16) static unsigned char st[16384];
        for (int t : {(int)Audio::EMIT_FIRE, (int)Audio::EMIT_ENGINE, (int)Audio::EMIT_CROWD, (int)Audio::EMIT_SIREN}) {
            EmitterSynth* s = constructEmitterSynth((Audio::EmitterType)t, st, 77);
            s->setParams(0.8f, 0.5f, 0.5f, 2.f);
            float buf[64];
            double t0 = TimeSeconds();
            for (int i = 0; i < 48000 * 10 / 64; i++) s->render(buf, 64);
            double t1 = TimeSeconds();
            printf("ftz %d type %d: %.1f ns/sample\n", ftz, t, (t1 - t0) / (48000 * 10) * 1e9);
            s->~EmitterSynth();
        }
    }
}
