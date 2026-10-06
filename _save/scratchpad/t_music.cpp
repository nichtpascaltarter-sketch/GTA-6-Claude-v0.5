#include <cstdarg>
#include <chrono>
#include "audio/audio_internal.h"
#include "audio/emitters.cpp"
#include "audio/sfx.cpp"
#include "audio/music.cpp"
void LogPrintf(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
void FatalError(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); exit(1); }
std::string StrFormat(const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); return b; }
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
using namespace Audio::detail;
static void writeWav(const char* path, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f = fopen(path, "wb"); int n = (int)L.size();
    int dataBytes = n * 4; int riff = 36 + dataBytes;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    int fmtLen = 16; short fmt = 1, ch = 2; int sr = 48000; int br = sr * 4; short ba = 4, bits = 16;
    fwrite(&fmtLen, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    for (int i = 0; i < n; i++) { short l = (short)Clamp((int)(L[i] * 32767), -32767, 32767), r = (short)Clamp((int)(R[i] * 32767), -32767, 32767); fwrite(&l, 2, 1, f); fwrite(&r, 2, 1, f); }
    fclose(f);
}
int main(int argc, char** argv) {
    music::pianoStartAsync(); music::pianoWait();
    int secs = argc > 1 ? atoi(argv[1]) : 30;
    for (int g = 0; g < 9; g++) {
        u32 seed = 1234 + g * 77;
        double t0 = TimeSeconds();
        auto sd = music::composeSong((music::Genre)g, seed);
        double t1 = TimeSeconds();
        music::SongPlayer pl;
        pl.start(sd, 0);
        double t2 = TimeSeconds();
        int n = secs * 48000;
        std::vector<float> L(n), R(n);
        for (int i = 0; i < n; i += 512) pl.render(&L[i], &R[i], Min(512, n - i));
        double t3 = TimeSeconds();
        double sum = 0; float pk = 0;
        for (int i = 0; i < n; i++) { sum += L[i]*L[i] + R[i]*R[i]; pk = Max(pk, Max(fabsf(L[i]), fabsf(R[i]))); }
        printf("genre %d bpm %.0f bars %d dur %.1fs events %zu tracks %zu: compose %.1fms kit %.1fms render %.2fx realtime, rms %.1f dB peak %.2f\n",
            g, sd->bpm, (int)(sd->musicEnd / (240.f/sd->bpm*48000)), sd->length/48000.0, sd->events.size(), sd->tracks.size(), (t1-t0)*1000, (t2-t1)*1000, (t3-t2)/secs*1.0, 10*log10(sum/(2*n)+1e-12), pk);
        char path[256]; snprintf(path, sizeof path, "song_%d.wav", g);
        writeWav(path, L, R);
    }
}
