#include <cstdarg>
#include <chrono>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
static void wav(const char* path, const std::vector<float>& x, int sr) {
    FILE* f = fopen(path, "wb"); u32 n = (u32)x.size() * 2;
    fwrite("RIFF", 1, 4, f); u32 v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); u16 h = 1; fwrite(&h, 2, 1, f); fwrite(&h, 2, 1, f); v = sr; fwrite(&v, 4, 1, f);
    v = sr * 2; fwrite(&v, 4, 1, f); h = 2; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f); fwrite("data", 1, 4, f);
    fwrite(&n, 4, 1, f); for (float s : x) { i16 q = (i16)lrintf(std::max(-1.f, std::min(1.f, s)) * 32767); fwrite(&q, 2, 1, f); }
    fclose(f);
}
int main() {
    const char* text = "Hey, get in the car! Is that the police? Drive, drive, drive!";
    struct V { const char* n; float p, fs, sp, br, ro, ex; } vs[] = {
        {"slow", 110, 1.0f, 0.5f, 0.1f, 0.0f, 1.0f}, {"fast", 110, 1.0f, 2.0f, 0.1f, 0.0f, 1.0f},
        {"child", 300, 1.32f, 1.1f, 0.2f, 0.0f, 1.5f}, {"deep", 70, 0.9f, 0.9f, 0.05f, 0.8f, 0.6f},
        {"monotone", 120, 1.0f, 1.0f, 0.1f, 0.0f, 0.0f}, {"wild", 200, 1.15f, 1.0f, 0.2f, 0.0f, 2.5f},
        {"breathy", 190, 1.15f, 1.0f, 1.0f, 0.0f, 1.0f}, {"rough", 100, 0.95f, 1.0f, 0.1f, 1.0f, 1.0f},
        {"clamped", 5000, 9.0f, 0.0f, -1.0f, 7.0f, -3.0f}};
    for (auto& v : vs) {
        Audio::VoiceParams p; p.pitch = v.p; p.formantScale = v.fs; p.speed = v.sp; p.breathiness = v.br;
        p.roughness = v.ro; p.expressiveness = v.ex;
        for (int sr : {22050, 48000}) {
            std::vector<float> x; Speech::synthesize(text, p, sr, x);
            bool fin = true; float pk = 0; double s2 = 0;
            for (float s : x) { fin = fin && s == s; pk = std::max(pk, fabsf(s)); s2 += s * s; }
            printf("%-9s %5d Hz: %.2fs (est %.2fs) peak %.2f rms %.3f finite %d\n", v.n, sr, x.size() / (double)sr,
                   Speech::estimateDuration(text, p), pk, sqrt(s2 / std::max<size_t>(1, x.size())), fin);
            if (sr == 22050) { char path[128]; snprintf(path, sizeof path, "/tmp/sp/ext_%s.wav", v.n); wav(path, x, sr); }
        }
    }
}
