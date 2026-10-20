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
static double cpuNow() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }
static const char* kEng[] = {"Sub", "Fm", "Pluck", "Piano", "Vox", "Organ", "808", "Drums"};
static double run(std::shared_ptr<SongData> sd, int frames) {
    SongPlayer p;
    p.start(sd, 0);
    std::vector<float> L((size_t)frames), R((size_t)frames);
    double c0 = cpuNow();
    for (int i = 0; i < frames; i += kProdBlock) p.render(&L[(size_t)i], &R[(size_t)i], kProdBlock);
    return cpuNow() - c0;
}
int main(int argc, char** argv) {
    int g = argc > 1 ? atoi(argv[1]) : 4;
    u32 seed = argc > 2 ? (u32)atoi(argv[2]) : 25210u;
    pianoStartAsync(); pianoWait();
    auto sd = composeSong((Genre)g, seed);
    int frames = Min((int)sd->length, 90 * 48000);
    frames -= frames % kProdBlock;
    double full = run(sd, frames);
    double secs = frames / 48000.0;
    printf("full: %.2f%% of a core\n", 100 * full / secs);
    auto empty = std::make_shared<SongData>(*sd);
    empty->events.clear();
    double base = run(empty, frames);
    printf("no events (mix + fx only): %.2f%%\n", 100 * base / secs);
    for (size_t t = 0; t < sd->tracks.size(); t++) {
        auto x = std::make_shared<SongData>(*sd);
        x->events.erase(std::remove_if(x->events.begin(), x->events.end(), [&](const NoteEv& e) { return e.track == t; }), x->events.end());
        double c = run(x, frames);
        int n = 0;
        for (auto& e : sd->events) if (e.track == t) n++;
        printf("  track %zu %-6s poly %2d events %5d: %.2f%%\n", t, kEng[(int)sd->tracks[t].patch.eng], sd->tracks[t].patch.poly, n, 100 * (full - c) / secs);
    }
}
