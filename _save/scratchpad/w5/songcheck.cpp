#include <cstdarg>
#include <chrono>
#include <time.h>
#include "audio/audio_all.cpp"
#include "../tests/audio/speech_stub.cpp"
void LogPrintf(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
void FatalError(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); exit(1); }
std::string StrFormat(const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); return b; }
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
using namespace Audio::detail;
using namespace Audio::detail::music;
static const char* kGenre[] = {"synthwave", "hiphop", "reggaeton", "house", "rock", "jazz", "country", "lofi"};
static double cpuNow() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }
int main(int argc, char** argv) {
    int songs = argc > 1 ? atoi(argv[1]) : 4;
    int only = argc > 2 ? atoi(argv[2]) : -1;
    pianoStartAsync();
    pianoWait();
    int bad = 0;
    for (int g = 0; g < 8; g++) {
        if (only >= 0 && g != only) continue;
        double worstCpu = 0, sumCpu = 0, audio = 0;
        for (int s = 0; s < songs; s++) {
            u32 seed = 9000u + (u32)g * 977u + (u32)s * 6151u;
            SongPlan pl = planSong((Genre)g, seed);
            double c0 = cpuNow();
            auto sd = composeSong((Genre)g, seed);
            SongPlayer p;
            p.start(sd, 0);
            int frames = (int)sd->length;
            frames -= frames % kProdBlock;
            std::vector<float> L((size_t)frames), R((size_t)frames);
            for (int i = 0; i < frames; i += kProdBlock) p.render(&L[(size_t)i], &R[(size_t)i], kProdBlock);
            double cpu = cpuNow() - c0;
            double dur = frames / 48000.0;
            worstCpu = Max(worstCpu, cpu / dur);
            sumCpu += cpu;
            audio += dur;
            bool nan = false;
            double sum = 0, sq = 0;
            float pk = 0;
            for (int i = 0; i < frames; i++) {
                if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) nan = true;
                sum += L[(size_t)i] + R[(size_t)i];
                sq += (double)L[(size_t)i] * L[(size_t)i] + (double)R[(size_t)i] * R[(size_t)i];
                pk = Max(pk, Max(fabsf(L[(size_t)i]), fabsf(R[(size_t)i])));
            }
            double dc = sum / (2.0 * frames), rms = 10 * log10(sq / (2.0 * frames) + 1e-20);
            // 1 s windows over the music part
            std::vector<double> w;
            int musicEnd = Min(frames, (int)sd->musicEnd);
            for (int i = 0; i + 48000 <= musicEnd; i += 48000) {
                double e = 0;
                for (int k = 0; k < 48000; k++) e += (double)L[(size_t)(i + k)] * L[(size_t)(i + k)] + (double)R[(size_t)(i + k)] * R[(size_t)(i + k)];
                w.push_back(10 * log10(e / 96000.0 + 1e-20));
            }
            std::vector<double> ws = w;
            std::sort(ws.begin(), ws.end());
            double med = ws.empty() ? -99 : ws[ws.size() / 2], mx = ws.empty() ? -99 : ws.back();
            int silent = 0;
            for (size_t k = 2; k + 2 < w.size(); k++) if (w[k] < med - 30) silent++;
            bool fail = nan || pk > 1.0f || fabs(dc) > 0.01 || rms < -32 || rms > -9 || mx - med > 12 || silent > 0;
            if (fail) bad++;
            printf("%-9s seed %5u sub %d var %d %3.0f bpm %5.1f s  rms %5.1f dB  pk %.2f  dc %+.4f  max-med %4.1f  silentWin %d  key+%d  cpu %.2f%%%s\n", kGenre[g], seed, pl.sub,
                   pl.variant, pl.bpm, dur, rms, pk, dc, mx - med, silent, sd->keyShift, 100 * cpu / dur, fail ? "  <-- FAIL" : "");
        }
        printf("  %s: avg cpu %.2f%% worst %.2f%%\n", kGenre[g], 100 * sumCpu / audio, 100 * worstCpu);
    }
    printf("%d failures\n", bad);
    return bad ? 1 : 0;
}
