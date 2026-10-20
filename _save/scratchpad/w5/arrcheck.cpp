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
static const char* kGenre[] = {"synthwave", "hiphop", "reggaeton", "house", "rock", "jazz", "country", "lofi"};
static const char* kPart[] = {"Intro", "Verse", "Pre", "Chorus", "Bridge", "Breakdown", "Build", "Drop", "Solo", "Outro"};
int main(int argc, char** argv) {
    int songs = argc > 1 ? atoi(argv[1]) : 6;
    for (int g = 0; g < 8; g++) {
        int kc = 0, fxHits[DK_COUNT] = {}, harmony = 0, perc = 0;
        for (int s = 0; s < songs; s++) {
            u32 seed = 5000u + (u32)g * 131u + (u32)s * 7919u;
            SongPlan pl = planSong((Genre)g, seed);
            auto sd = composeSong((Genre)g, seed);
            if (sd->keyShift) kc++;
            if (s == 0) {
                printf("%s seed %u:", kGenre[g], seed);
                for (auto& sc : pl.sections) printf(" %s%d@%d", kPart[(int)sc.part], sc.bars, sc.startBar);
                printf("  tracks %zu keyShift %d at bar %.1f\n", sd->tracks.size(), sd->keyShift, sd->keyShiftAt / (4.0 * 60.0 / sd->bpm * 48000.0));
            }
            for (auto& e : sd->events)
                if (sd->tracks[e.track].drums && (e.note == DK_REV_CYM || e.note == DK_RISER || e.note == DK_IMPACT)) fxHits[e.note]++;
            (void)harmony; (void)perc;
        }
        printf("   %d/%d key changes, revcym %d riser %d impact %d\n", kc, songs, fxHits[DK_REV_CYM], fxHits[DK_RISER], fxHits[DK_IMPACT]);
    }
}
