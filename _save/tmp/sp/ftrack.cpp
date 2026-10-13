#include <cstdarg>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
using namespace Speech::detail;
int main(int argc, char** argv) {
    // usage: ftrack "text" PH  -> formant track around the first occurrence of phoneme PH after 0.6 s
    Audio::VoiceParams v; v.pitch = 110; v.formantScale = 1; v.speed = 1; v.breathiness = 0.1f; v.roughness = 0; v.expressiveness = 1;
    Utterance u; buildUtterance(argv[1], v, u);
    std::vector<F0Point> f0; buildF0(u, v, f0);
    std::vector<Frame> fr; std::vector<std::pair<float, float>> clicks;
    buildFrames(u, f0, v, fr, clicks);
    int target = -1;
    for (size_t i = 0; i < u.segs.size(); i++)
        if (!strcmp(phInfo(u.segs[i].ph).name, argv[2]) && u.segs[i].t0 > 0.5f) { target = (int)i; break; }
    if (target < 0) return 1;
    float a = u.segs[target].t0 - 0.01f, b = u.segs[target + 1].t0 + u.segs[target + 1].dur * 0.6f;
    for (size_t fi = 0; fi < fr.size(); fi++) {
        float t = fi * kFrameSec;
        if (t < a || t > b || (fi % 2)) continue;
        int seg = 0;
        while (seg + 1 < (int)u.segs.size() && u.segs[seg + 1].t0 <= t) seg++;
        printf("%.3f %-3s F1 %4.0f F2 %4.0f F3 %4.0f  av %.3f af %.4f\n", t, phInfo(u.segs[seg].ph).name, fr[fi].f[0], fr[fi].f[1], fr[fi].f[2], fr[fi].av, fr[fi].af);
    }
}
