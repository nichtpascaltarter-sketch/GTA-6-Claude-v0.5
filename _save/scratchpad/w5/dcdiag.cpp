#include <cstdarg>
#include <chrono>
#include "audio/audio_all.cpp"
#include "../tests/audio/speech_stub.cpp"
void LogPrintf(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
void FatalError(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); exit(1); }
std::string StrFormat(const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); return b; }
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
using namespace Audio::detail;
using namespace Audio::detail::music;
int main(int argc, char** argv) {
    int g = argc > 1 ? atoi(argv[1]) : 7;
    u32 seed = argc > 2 ? (u32)atoi(argv[2]) : 424242u + (u32)g * 1013u;
    pianoStartAsync(); pianoWait();
    radio::ensureStations();
    auto sd = composeSong((Genre)g, seed);
    SongPlan pl = planSong((Genre)g, seed);
    printf("sub %d variant %d bpm %.0f tracks %zu\n", pl.sub, pl.variant, pl.bpm, sd->tracks.size());
    // per-track DC: solo each track
    for (int t = -1; t < (int)sd->tracks.size(); t++) {
        auto solo = std::make_shared<SongData>(*sd);
        if (t >= 0) for (size_t k = 0; k < solo->tracks.size(); k++) if ((int)k != t) solo->tracks[k].gain = 0.f;
        SongPlayer sp;
        sp.start(solo, (u32)(50.f * kSR));
        int frames = 24 * 48000;
        frames -= frames % kProdBlock;
        std::vector<float> L((size_t)frames), R((size_t)frames);
        for (int i = 0; i < frames; i += kProdBlock) sp.render(&L[(size_t)i], &R[(size_t)i], kProdBlock);
        double s = 0; for (int i = 0; i < frames; i++) s += L[(size_t)i] + R[(size_t)i];
        double dcRaw = s / (2.0 * frames);
        radio::StationProducer prod;
        prod.station = g; prod.reset(g, 0.0);
        for (int i = 0; i < frames; i += kProdBlock) prod.process(&L[(size_t)i], &R[(size_t)i], kProdBlock);
        s = 0; for (int i = 0; i < frames; i++) s += L[(size_t)i] + R[(size_t)i];
        prod.station = -1;
        printf("track %2d: raw dc %+.5f  mastered dc %+.5f\n", t, dcRaw, s / (2.0 * frames));
    }
}
