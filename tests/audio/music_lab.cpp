// Music engine analysis tool: per genre, prints the song plan, per-track note statistics and the
// level / spectral centroid of every track rendered solo, plus the full-mix loudness. Optionally
// writes the full mix as WAV. Build:
//   g++ -std=c++17 -O2 -I src tests/audio/music_lab.cpp -o /tmp/music_lab -lpthread
//   /tmp/music_lab [genre|-1] [seed] [seconds] [outdir]
#include <cstdarg>
#include <chrono>
#include "audio/audio_all.cpp"
#include "speech_stub.cpp"

void LogPrintf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
}
void FatalError(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    exit(1);
}
std::string StrFormat(const char* fmt, ...) {
    char b[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    return b;
}
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

using namespace Audio::detail;
using namespace Audio::detail::music;

static const char* kGenre[] = {"synthwave", "hiphop", "reggaeton", "house", "rock", "jazz", "country", "lofi", "talk"};
static const char* kPart[] = {"Intro", "Verse", "Pre", "Chorus", "Bridge", "Breakdown", "Build", "Drop", "Solo", "Outro"};
static const char* kEng[] = {"Sub", "Fm", "Pluck", "Piano", "Vox", "Organ", "808", "Drums"};

static void writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    int n = (int)L.size(), dataBytes = n * 4, riff = 36 + dataBytes, fmtLen = 16, sr = 48000, br = sr * 4;
    short fmt = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    for (int i = 0; i < n; i++) {
        short l = (short)Clamp((int)lrintf(L[(size_t)i] * 32767.f), -32767, 32767), r = (short)Clamp((int)lrintf(R[(size_t)i] * 32767.f), -32767, 32767);
        fwrite(&l, 2, 1, f); fwrite(&r, 2, 1, f);
    }
    fclose(f);
}

static void renderSong(std::shared_ptr<SongData> sd, int frames, std::vector<float>& L, std::vector<float>& R) {
    SongPlayer p;
    p.start(sd, 0);
    L.assign((size_t)frames, 0.f);
    R.assign((size_t)frames, 0.f);
    for (int i = 0; i < frames; i += kProdBlock) p.render(&L[(size_t)i], &R[(size_t)i], Min(kProdBlock, frames - i));
}

static void levels(const std::vector<float>& L, const std::vector<float>& R, float& rmsDb, float& centroid, float& peak) {
    double s = 0;
    peak = 0;
    for (size_t i = 0; i < L.size(); i++) {
        s += L[i] * L[i] + R[i] * R[i];
        peak = Max(peak, Max(fabsf(L[i]), fabsf(R[i])));
    }
    rmsDb = (float)(10 * log10(s / (2.0 * (double)L.size()) + 1e-12));
    // zero-crossing-free centroid estimate via first-difference energy ratio
    double d = 0;
    for (size_t i = 1; i < L.size(); i++) {
        float x = (L[i] + R[i]) - (L[i - 1] + R[i - 1]);
        d += x * x;
    }
    double e = 0;
    for (size_t i = 0; i < L.size(); i++) e += (L[i] + R[i]) * (L[i] + R[i]);
    // for a sinusoid of frequency f: diff energy / energy = (2 sin(pi f / fs))^2
    double ratio = e > 0 ? d / e : 0;
    double sn = sqrt(ratio) / 2.0;
    centroid = (float)(asin(Min(1.0, sn)) / 3.14159265 * 48000.0);
}

static const char* kNoteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static std::string nn(int m) { return StrFormat("%s%d", kNoteNames[m % 12], m / 12 - 1); }

// Prints bars [b0, b1) of a song: chord per beat and each melodic track's notes with positions.
static void dumpBars(const SongData& sd, int b0, int b1) {
    float spb = 60.f / sd.bpm * 48000.f;
    for (int bar = b0; bar < b1; bar++) {
        printf("  bar %d\n", bar);
        for (size_t t = 0; t < sd.tracks.size(); t++) {
            if (sd.tracks[t].drums) continue;
            std::string line;
            for (auto& e : sd.events) {
                if (e.track != t) continue;
                float beat = (float)e.start / spb;
                if (beat < bar * 4.f - 0.01f || beat >= (bar + 1) * 4.f - 0.01f) continue;
                line += StrFormat(" %s@%.2f(%.2f)", nn(e.note).c_str(), beat - bar * 4.f, (float)e.dur / spb);
            }
            if (!line.empty()) printf("    t%zu %-6s:%s\n", t, kEng[(int)sd.tracks[t].patch.eng], line.c_str());
        }
        std::string dl;
        for (auto& e : sd.events) {
            if (!sd.tracks[e.track].drums || e.track != 0) continue;
            float beat = (float)e.start / spb;
            if (beat < bar * 4.f - 0.01f || beat >= (bar + 1) * 4.f - 0.01f) continue;
            static const char* dn[] = {"K", "S", "C", "h", "o", "p", "R", "b", "X", "tl", "tm", "th", "rim", "sh", "tb", "cb", "cl", "ch", "bo", "Tl", "Th", "cv", "gu", "sn", "bs", "bt", "ris", "imp", "rev", "sub"};
            dl += StrFormat(" %s%.2f", dn[e.note], beat - bar * 4.f);
        }
        printf("    drums :%s\n", dl.c_str());
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "dump")) {
        int g = argc > 2 ? atoi(argv[2]) : 0;
        u32 seed = argc > 3 ? (u32)atoi(argv[3]) : 1234u;
        int b0 = argc > 4 ? atoi(argv[4]) : 0, b1 = argc > 5 ? atoi(argv[5]) : 8;
        SongPlan pl = planSong((Genre)g, seed);
        auto sd = composeSong((Genre)g, seed);
        printf("%s: %.0f bpm key %s scale %d variant %d\n  form:", kGenre[g], pl.bpm, kNoteNames[pl.key.tonic], (int)pl.key.scale, pl.variant);
        for (auto& s : pl.sections) printf(" %s%d@%d", kPart[(int)s.part], s.bars, s.startBar);
        printf("\n");
        dumpBars(*sd, b0, b1);
        return 0;
    }
    int only = argc > 1 ? atoi(argv[1]) : -1;
    u32 seed = argc > 2 ? (u32)atoi(argv[2]) : 1234u;
    float secs = argc > 3 ? (float)atof(argv[3]) : 0.f;
    std::string outdir = argc > 4 ? argv[4] : "";
    pianoStartAsync();
    pianoWait();
    for (int g = 0; g < 8; g++) {
        if (only >= 0 && g != only) continue;
        u32 sdSeed = seed + (u32)g * 77u;
        SongPlan pl = planSong((Genre)g, sdSeed);
        auto sd = composeSong((Genre)g, sdSeed);
        printf("\n=== %s seed %u: %.0f bpm, key %d scale %d, variant %d, sub-style %d, %d bars, %.1f s, swing %.2f, last-chorus key change %+d\n", kGenre[g],
               sdSeed, pl.bpm, pl.key.tonic, (int)pl.key.scale, pl.variant, pl.sub, pl.totalBars, pl.durationSec(), pl.swing, sd->keyShift);
        printf("  form:");
        for (auto& s : pl.sections) printf(" %s%d", kPart[(int)s.part], s.bars);
        printf("\n");
        int frames = secs > 0 ? (int)(secs * 48000.f) : (int)sd->length;
        std::vector<float> L, R;
        renderSong(sd, frames, L, R);
        float mr, mc, mp;
        levels(L, R, mr, mc, mp);
        printf("  MIX rms %.1f dB, centroid %.0f Hz, peak %.2f\n", mr, mc, mp);
        if (!outdir.empty()) writeWav(outdir + "/lab_" + kGenre[g] + ".wav", L, R);
        for (size_t t = 0; t < sd->tracks.size(); t++) {
            // note stats
            int cnt = 0, lo = 127, hi = 0, outKey = 0, steps = 0, leaps = 0, ivs = 0, strong = 0, strongCt = 0;
            int prev = -1;
            for (auto& e : sd->events) {
                if (e.track != t) continue;
                cnt++;
                if (sd->tracks[t].drums) continue;
                lo = Min(lo, (int)e.note);
                hi = Max(hi, (int)e.note);
                if (!inScale(sd->key, e.note)) outKey++;
                if (prev >= 0) {
                    int d = abs((int)e.note - prev);
                    ivs++;
                    if (d <= 2) steps++;
                    if (d > 7) leaps++;
                }
                prev = e.note;
            }
            (void)strong;
            (void)strongCt;
            auto solo = std::make_shared<SongData>(*sd);
            for (size_t k = 0; k < solo->tracks.size(); k++)
                if (k != t) solo->tracks[k].gain = 0.f;
            std::vector<float> sl, sr;
            renderSong(solo, frames, sl, sr);
            float r, c, pk;
            levels(sl, sr, r, c, pk);
            const TrackDef& td = sd->tracks[t];
            if (td.drums)
                printf("  track %zu %-6s notes %5d                                  rms %6.1f dB  centroid %5.0f Hz  peak %.2f\n", t, "Drums", cnt, r, c, pk);
            else
                printf("  track %zu %-6s notes %5d range %3d-%3d outKey %4.1f%% steps %3.0f%% leaps %3.0f%%  rms %6.1f dB  centroid %5.0f Hz  peak %.2f\n", t,
                       kEng[(int)td.patch.eng], cnt, lo, hi, cnt ? 100.f * outKey / cnt : 0.f, ivs ? 100.f * steps / ivs : 0.f,
                       ivs ? 100.f * leaps / ivs : 0.f, r, c, pk);
        }
    }
}
