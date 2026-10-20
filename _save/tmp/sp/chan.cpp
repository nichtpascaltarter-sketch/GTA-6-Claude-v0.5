#include <cstdarg>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
static void wav(const std::string& path, const std::vector<float>& x, int sr) {
    FILE* f = fopen(path.c_str(), "wb"); u32 n = (u32)x.size() * 2;
    fwrite("RIFF", 1, 4, f); u32 v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); u16 h = 1; fwrite(&h, 2, 1, f); fwrite(&h, 2, 1, f); v = sr; fwrite(&v, 4, 1, f);
    v = sr * 2; fwrite(&v, 4, 1, f); h = 2; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f); fwrite("data", 1, 4, f);
    fwrite(&n, 4, 1, f); for (float s : x) { i16 q = (i16)lrintf(std::max(-1.f, std::min(1.f, s)) * 32767); fwrite(&q, 2, 1, f); }
    fclose(f);
}
static double now() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
int main() {
    const char* lines[][2] = {{"megaphone", "[megaphone][shout]This is the police! Step out of the vehicle with your hands up!"},
                              {"pa", "[pa][calm]Attention passengers. Flight two one seven to Coral Keys is now boarding at gate twelve."},
                              {"radio", "[radio][dispatch]All units, suspect heading north on Ocean Drive in a red sedan."},
                              {"phone", "[phone]Hey, it's me. Meet me at the docks at ten, and come alone."}};
    Audio::VoiceParams v = Speech::persona("cop").voice;
    for (auto& l : lines) {
        std::vector<float> x; Speech::synthesize(l[1], v, 44100, x);
        float pk = 0; bool fin = true; for (float s : x) { fin = fin && s == s; pk = std::max(pk, fabsf(s)); }
        std::vector<Speech::VisemeKey> k; Speech::lipSync(l[1], v, k);
        printf("%-10s dur %.2f est %.2f lipEnd %.2f peak %.2f %s\n", l[0], x.size() / 44100.0, Speech::estimateDuration(l[1], v), k.back().time + k.back().duration, pk, fin ? "" : "NaN");
        wav(std::string("/tmp/sp/an/ch_") + l[0] + ".wav", x, 44100);
    }
    for (float ex : {0.1f, 0.9f}) {
        Speech::WallaParams wp; wp.seed = 7; wp.voices = 8; wp.seconds = 12.f; wp.excitement = ex; wp.accent = Speech::ACCENT_LATINO; wp.accentMix = 0.4f;
        std::vector<float> x; double t0 = now(); Speech::walla(wp, 22050, x); double t1 = now();
        float pk = 0; double s2 = 0; bool fin = true; for (float s : x) { fin = fin && s == s; pk = std::max(pk, fabsf(s)); s2 += s * s; }
        float jump = fabsf(x.front() - x.back());
        printf("walla ex %.1f: %.2f s, cpu %.2f s, peak %.2f rms %.3f loopJump %.4f %s\n", ex, x.size() / 22050.0, t1 - t0, pk, sqrt(s2 / x.size()), jump, fin ? "" : "NaN");
        wav(ex < 0.5f ? "/tmp/sp/an/walla_calm.wav" : "/tmp/sp/an/walla_lively.wav", x, 22050);
    }
}
