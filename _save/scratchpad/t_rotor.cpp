#include <cstdarg>
#include <chrono>
#include "audio/audio_internal.h"
#include "audio/emitters.cpp"
void LogPrintf(const char*, ...) {}
void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
using namespace Audio::detail;
int main() {
    emit::RotorCore rc; rc.init(1234); rc.setParams(1.f, 0.7f);
    double sum = 0; int n = 48000 * 5;
    for (int i = 0; i < n; i++) sum += rc.tick();
    printf("rotor mean %.4f\n", sum / n);
    // component test: resonator kicked
    Audio::dsp::Resonator r; r.set(70.f, 0.035f); double s2 = 0; r.y1 += 1.f;
    for (int i = 0; i < 48000; i++) s2 += r.process(0.f);
    printf("resonator kick sum %.4f\n", s2);
}
