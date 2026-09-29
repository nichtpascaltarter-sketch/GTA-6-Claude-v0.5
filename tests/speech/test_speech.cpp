// Native test harness for the procedural TTS (src/audio/speech.cpp).
// Build:  g++ -std=c++17 -O2 -I src tests/speech/test_speech.cpp -o /tmp/test_speech -lpthread
// Usage:  /tmp/test_speech [--out DIR] [--rate HZ] [--say "text"] [--voice NAME] [--phon "text"] [--quick]
//                          [--words] [--focus FILE] [--segs "text"]
// Writes one WAV per (sentence, voice) plus manifest.tsv / manifest_heldout.tsv (for tests/speech/asr_eval.py),
// prints phoneme transcriptions, timing (x real time) and sanity checks (NaN, clipping, silence, duration
// estimate, determinism, threads, text-normalization regressions). --words writes the rhyme-test style word set
// ("Say the word X again."), --focus synthesizes each line of FILE with every test voice, --segs dumps segment
// timing for spectrogram overlays. Exit code 1 when any check fails.
#include <chrono>
#include <cstdarg>
#include <thread>
#include <sys/stat.h>

#include "audio/speech.cpp"

// ---- minimal platform stubs required by core/base.h
void LogPrintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}
[[noreturn]] void FatalError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}
std::string StrFormat(const char* fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}
double TimeSeconds() {
    using namespace std::chrono;
    static const auto t0 = steady_clock::now();
    return duration<double>(steady_clock::now() - t0).count();
}

static bool writeWav(const std::string& path, const std::vector<float>& x, int sr) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    u32 dataBytes = (u32)x.size() * 2;
    auto w32 = [&](u32 v) { fwrite(&v, 4, 1, f); };
    auto w16 = [&](u16 v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    w32(36 + dataBytes);
    fwrite("WAVEfmt ", 1, 8, f);
    w32(16);
    w16(1);
    w16(1);
    w32((u32)sr);
    w32((u32)sr * 2);
    w16(2);
    w16(16);
    fwrite("data", 1, 4, f);
    w32(dataBytes);
    for (float v : x) {
        float c = v < -1.f ? -1.f : (v > 1.f ? 1.f : v);
        i16 s = (i16)lrintf(c * 32767.f);
        fwrite(&s, 2, 1, f);
    }
    fclose(f);
    return true;
}

struct NamedVoice {
    const char* name;
    Audio::VoiceParams v;
};

static std::vector<NamedVoice> testVoices() {
    std::vector<NamedVoice> vs;
    Audio::VoiceParams m;
    m.pitch = 112.f, m.formantScale = 1.0f, m.speed = 1.0f, m.breathiness = 0.1f, m.roughness = 0.03f, m.expressiveness = 1.0f;
    vs.push_back({"male", m});
    Audio::VoiceParams f;
    f.pitch = 205.f, f.formantScale = 1.17f, f.speed = 1.0f, f.breathiness = 0.2f, f.roughness = 0.f, f.expressiveness = 1.1f;
    vs.push_back({"female", f});
    Audio::VoiceParams o;
    o.pitch = 92.f, o.formantScale = 0.95f, o.speed = 0.92f, o.breathiness = 0.08f, o.roughness = 0.45f, o.expressiveness = 0.8f;
    vs.push_back({"oldgruff", o});
    vs.push_back({"presetM", Speech::presetVoice(false, 7)});
    vs.push_back({"presetF", Speech::presetVoice(true, 11)});
    return vs;
}

// Test sentences: text to synthesize, and the spoken-form reference for ASR scoring ("a|b" = either word).
struct TestSentence {
    const char* text;
    const char* ref;
};
static const TestSentence kSentences[] = {
    {"Get in the car, we have to go now!", nullptr},
    {"The police are coming. Drive!", nullptr},
    {"Welcome to Porto Sol, the city that never sleeps.", "Welcome to Porto Sol|soul, the city that never sleeps."},
    {"You've got two hundred and fifty dollars. Is that all?", nullptr},
    {"It's ten thirty on a beautiful Tuesday morning.", nullptr},
    {"I need the money by tomorrow night, or you're dead.", nullptr},
    {"Put your hands up and step away from the vehicle.", nullptr},
    {"Suspect is heading north on the highway in a red sedan.", nullptr},
    {"Hey, watch where you're going, buddy!", nullptr},
    {"My brother works down at the docks near the beach.", nullptr},
    {"Don't worry about it, I'll take care of everything.", nullptr},
    {"This is the hottest station in the city, playing the best music all day long.", nullptr},
    {"The birch canoe slid on the smooth planks.", nullptr},
    {"Glue the sheet to the dark blue background.", nullptr},
    {"It's easy to tell the depth of a well.", nullptr},
    {"These days a chicken leg is a rare dish.", nullptr},
    {"Rice is often served in round bowls.", nullptr},
    {"The juice of lemons makes fine punch.", nullptr},
    {"The box was thrown beside the parked truck.", nullptr},
    {"The hogs were fed chopped corn and garbage.", nullptr},
    {"Four hours of steady work faced us.", nullptr},
    {"A large size in stockings is hard to sell.", nullptr},
    {"Meet me at 10:30 on 5th Ave. with $1,500.", "Meet me at 10:30 on 5th Avenue with $1,500."},
};
// Held-out sentences: never used for tuning; they measure generalization (Harvard list 2 + new game lines).
static const TestSentence kHeldOut[] = {
    {"The boy was there when the sun rose.", nullptr},
    {"A rod is used to catch pink salmon.", nullptr},
    {"The source of the huge river is the clear spring.", nullptr},
    {"Kick the ball straight and follow through.", nullptr},
    {"Help the woman get back to her feet.", nullptr},
    {"A pot of tea helps to pass the evening.", nullptr},
    {"Smoky fires lack flame and heat.", "Smoky|smokey fires lack flame and heat."},
    {"The soft cushion broke the man's fall.", nullptr},
    {"The salt breeze came across from the sea.", nullptr},
    {"The girl at the booth sold fifty bonds.", nullptr},
    {"Keep your eyes on the road and your hands on the wheel.", nullptr},
    {"We lost them near the old bridge, turn left at the gas station.", nullptr},
    {"Nobody moves, nobody gets hurt.", nullptr},
    {"You'll never make it out of this town alive.", nullptr},
    {"All units, shots fired at the bank on Ocean Drive.", nullptr},
    {"Tonight's weather: clear skies and a light breeze off the water.", nullptr},
    {"I told you, the package has to be there by midnight.", nullptr},
    {"Why would anyone steal a garbage truck?", nullptr},
};

struct Stats {
    double synthSec = 0, audioSec = 0;
    int problems = 0;
};

static void checkBuffer(const char* tag, const std::vector<float>& x, int sr, float est, Stats& st) {
    float peak = 0.f;
    double sum2 = 0;
    int nans = 0;
    for (float v : x) {
        if (!(v == v)) nans++;
        peak = std::max(peak, fabsf(v));
        sum2 += (double)v * v;
    }
    float dur = (float)x.size() / sr;
    float rms = x.empty() ? 0.f : (float)sqrt(sum2 / x.size());
    // largest sample-to-sample jump relative to peak (click detector)
    float maxJump = 0.f;
    for (size_t i = 1; i < x.size(); i++) maxJump = std::max(maxJump, fabsf(x[i] - x[i - 1]));
    bool bad = nans > 0 || peak > 0.81f || peak < 0.5f || rms < 0.02f || fabsf(dur - est) > 0.05f + 0.02f * dur;
    if (bad) st.problems++;
    printf("  %-10s dur %.2fs (est %.2fs) peak %.3f rms %.3f maxJump %.3f %s\n", tag, dur, est, peak, rms, maxJump,
           bad ? "<-- PROBLEM" : "");
}

int main(int argc, char** argv) {
    std::string outDir = "/tmp/speech_out";
    int rate = 22050;
    const char* say = nullptr;
    const char* phon = nullptr;
    const char* segsText = nullptr;
    bool wordTest = false;
    const char* focusFile = nullptr;
    const char* onlyVoice = nullptr;
    bool quick = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (a == "--rate" && i + 1 < argc) rate = atoi(argv[++i]);
        else if (a == "--say" && i + 1 < argc) say = argv[++i];
        else if (a == "--phon" && i + 1 < argc) phon = argv[++i];
        else if (a == "--segs" && i + 1 < argc) segsText = argv[++i];
        else if (a == "--voice" && i + 1 < argc) onlyVoice = argv[++i];
        else if (a == "--quick") quick = true;
        else if (a == "--words") wordTest = true;
        else if (a == "--focus" && i + 1 < argc) focusFile = argv[++i];
    }
    mkdir(outDir.c_str(), 0755);
    if (phon) {
        printf("%s\n", Speech::detail::debugPhonemes(phon).c_str());
        return 0;
    }
    if (focusFile) {
        // Focused experiment: each line of the file is a sentence; all test voices.
        FILE* f = fopen(focusFile, "r");
        FILE* mf = fopen((outDir + "/manifest_focus.tsv").c_str(), "w");
        char line[1024];
        int li = 0;
        std::vector<NamedVoice> vs = testVoices();
        while (f && fgets(line, sizeof(line), f)) {
            std::string t = line;
            while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
            if (t.empty()) continue;
            std::string ref = t, text = t;
            size_t bar = t.find('\t');
            if (bar != std::string::npos) text = t.substr(0, bar), ref = t.substr(bar + 1);
            for (auto& nv : vs) {
                std::vector<float> x;
                Speech::synthesize(text.c_str(), nv.v, rate, x);
                char name[128];
                snprintf(name, sizeof(name), "f%02d_%s.wav", li, nv.name);
                writeWav(outDir + "/" + name, x, rate);
                fprintf(mf, "%s\t%s\t%s\n", name, nv.name, ref.c_str());
            }
            li++;
        }
        if (f) fclose(f);
        fclose(mf);
        return 0;
    }
    if (wordTest) {
        // Rhyme-test style diagnostic: each word in the carrier phrase "Say the word X again."
        static const char* const kWords[] = {
            // initial consonants (with /ae/ or /a/)
            "pat", "bat", "tap", "dad", "cat", "gap", "fat", "vat", "that", "sat", "zap", "chat", "jab", "shack",
            "mat", "nap", "lap", "rat", "wax", "yak", "hat", "thank",
            // final consonants
            "cap", "cab", "cut", "cud", "back", "bag", "calf", "have", "bath", "bass", "jazz", "cash", "badge",
            "catch", "ham", "can", "bang", "pal", "car",
            // vowels
            "beat", "bit", "bait", "bet", "bought", "boat", "book", "boot", "but", "bird", "bite", "bout", "boy",
            "pot", "hot", "part", "heart", "cot", "cart", "salt", "south", "well", "whale", "hole", "howl",
            // clusters
            "stop", "spin", "skip", "street", "split", "scream", "black", "bring", "clean", "crash", "drive",
            "dream", "flat", "fry", "glass", "green", "play", "pray", "slow", "smoke", "snake", "swim", "three",
            "train", "twin", "quick", "shrink",
        };
        FILE* mw = fopen((outDir + "/manifest_words.tsv").c_str(), "w");
        std::vector<NamedVoice> vs = testVoices();
        for (auto& nv : vs) {
            if (onlyVoice && strcmp(onlyVoice, nv.name)) continue;
            for (size_t k = 0; k < ARRAY_COUNT(kWords); k++) {
                std::string text = std::string("Say the word ") + kWords[k] + " again.";
                std::vector<float> x;
                Speech::synthesize(text.c_str(), nv.v, rate, x);
                char name[128];
                snprintf(name, sizeof(name), "w_%s_%s.wav", nv.name, kWords[k]);
                writeWav(outDir + "/" + name, x, rate);
                fprintf(mw, "%s\t%s\t%s\n", name, nv.name, text.c_str());
            }
        }
        fclose(mw);
        return 0;
    }
    if (segsText) {
        // Segment dump (phoneme, start, duration, VOT) for spectrogram overlays, plus the WAV.
        std::vector<NamedVoice> vs = testVoices();
        const Audio::VoiceParams& v = vs[0].v;
        Speech::detail::Utterance utt;
        Speech::detail::buildUtterance(segsText, v, utt);
        FILE* f = fopen((outDir + "/segs.txt").c_str(), "w");
        for (const auto& sg : utt.segs) {
            fprintf(f, "%s %.4f %.4f %.4f %u\n", Speech::detail::phInfo(sg.ph).name, sg.t0, sg.dur, sg.vot, sg.flags);
            printf("%-4s t0 %.3f dur %.3f vot %.3f stress %d flags %05x\n", Speech::detail::phInfo(sg.ph).name, sg.t0,
                   sg.dur, sg.vot, sg.stress, sg.flags);
        }
        fclose(f);
        std::vector<float> x;
        Speech::synthesize(segsText, v, rate, x);
        writeWav(outDir + "/segs.wav", x, rate);
        return 0;
    }
    std::vector<NamedVoice> voices = testVoices();
    if (say) {
        for (auto& nv : voices) {
            if (onlyVoice && strcmp(onlyVoice, nv.name)) continue;
            std::vector<float> x;
            Speech::synthesize(say, nv.v, rate, x);
            std::string p = outDir + "/say_" + nv.name + ".wav";
            writeWav(p, x, rate);
            printf("%s: %zu samples -> %s\n", nv.name, x.size(), p.c_str());
        }
        printf("%s\n", Speech::detail::debugPhonemes(say).c_str());
        return 0;
    }

    printf("Voices:\n");
    for (auto& nv : voices)
        printf("  %-9s pitch %.0f fs %.2f speed %.2f breath %.2f rough %.2f expr %.2f\n", nv.name, nv.v.pitch,
               nv.v.formantScale, nv.v.speed, nv.v.breathiness, nv.v.roughness, nv.v.expressiveness);
    printf("Preset variety:\n");
    for (u32 s = 0; s < 8; s++) {
        Audio::VoiceParams m = Speech::presetVoice(false, s), f = Speech::presetVoice(true, s);
        printf("  seed %u  M: %.0fHz fs %.2f sp %.2f br %.2f ro %.2f ex %.2f | F: %.0fHz fs %.2f sp %.2f br %.2f ro %.2f ex %.2f\n",
               s, m.pitch, m.formantScale, m.speed, m.breathiness, m.roughness, m.expressiveness, f.pitch,
               f.formantScale, f.speed, f.breathiness, f.roughness, f.expressiveness);
    }

    FILE* man = fopen((outDir + "/manifest.tsv").c_str(), "w");
    FILE* manHeld = fopen((outDir + "/manifest_heldout.tsv").c_str(), "w");
    Stats st;
    size_t nMain = quick ? 5 : ARRAY_COUNT(kSentences);
    size_t nSent = quick ? 5 : ARRAY_COUNT(kSentences) + ARRAY_COUNT(kHeldOut);
    for (size_t si = 0; si < nSent; si++) {
        const TestSentence& ts = si < ARRAY_COUNT(kSentences) ? kSentences[si] : kHeldOut[si - ARRAY_COUNT(kSentences)];
        const char* text = ts.text;
        FILE* mf = si < nMain ? man : manHeld;
        printf("[%zu] %s\n    %s\n", si, text, Speech::detail::debugPhonemes(text).c_str());
        for (auto& nv : voices) {
            if (onlyVoice && strcmp(onlyVoice, nv.name)) continue;
            std::vector<float> x;
            double t0 = TimeSeconds();
            Speech::synthesize(text, nv.v, rate, x);
            double t1 = TimeSeconds();
            st.synthSec += t1 - t0;
            st.audioSec += (double)x.size() / rate;
            float est = Speech::estimateDuration(text, nv.v);
            checkBuffer(nv.name, x, rate, est, st);
            char name[256];
            snprintf(name, sizeof(name), "s%02zu_%s.wav", si, nv.name);
            writeWav(outDir + "/" + name, x, rate);
            if (mf) fprintf(mf, "%s\t%s\t%s\n", name, nv.name, ts.ref ? ts.ref : text);
        }
    }
    if (man) fclose(man);
    if (manHeld) fclose(manHeld);
    printf("Synthesis: %.1f ms for %.1f s of audio at %d Hz -> %.0fx real time\n", st.synthSec * 1000.0, st.audioSec,
           rate, st.audioSec / std::max(1e-9, st.synthSec));

    // Per-sentence timing at 48 kHz for a typical 3 s sentence.
    {
        std::vector<float> x;
        const char* text = "Welcome to Porto Sol, the city that never sleeps.";
        double best = 1e9;
        for (int k = 0; k < 5; k++) {
            x.clear();
            double t0 = TimeSeconds();
            Speech::synthesize(text, voices[0].v, 48000, x);
            best = std::min(best, TimeSeconds() - t0);
        }
        printf("48 kHz: '%s' %.2f s audio in %.2f ms (%.0fx real time)\n", text, x.size() / 48000.0, best * 1000.0,
               (x.size() / 48000.0) / best);
    }

    // Determinism and thread safety: parallel synthesis must equal sequential synthesis.
    {
        const int nT = 4;
        std::vector<std::vector<float>> seq(nT), par(nT);
        for (int k = 0; k < nT; k++) Speech::synthesize(kSentences[k].text, voices[k % voices.size()].v, 44100, seq[k]);
        std::vector<std::thread> th;
        for (int k = 0; k < nT; k++)
            th.emplace_back([&, k]() { Speech::synthesize(kSentences[k].text, voices[k % voices.size()].v, 44100, par[k]); });
        for (auto& t : th) t.join();
        bool same = true;
        for (int k = 0; k < nT; k++) same = same && seq[k] == par[k];
        printf("Thread/determinism check: %s\n", same ? "OK" : "MISMATCH");
        if (!same) st.problems++;
    }
    // Text normalization regressions (numbers, money, codes, homographs).
    {
        static const char* const kNorm[][2] = {
            {"Meet me at 10:30 on 5th Ave. with $1,500.",
             "meet me at ten thirty on fifth avenue with one thousand five hundred dollars"},
            {"It costs \xe2\x82\xac" "4.50 or \xc2\xa3" "2.99.",
             "it costs four euros and fifty cents or two pounds and ninety nine pence"},
            {"Take exit 12B, it's a 5K run.", "take exit twelve b it's a five k run"},
            {"Ages 18-25, call 555-0123.", "ages eighteen to twenty five call five five five oh one two three"},
            {"In 1986 he scored 102-98.", "in nineteen eighty six he scored one hundred two ninety eight"},
            {"The suspect fled; we suspect him.", "the {S AH1 S P EH2 K T} fled we {S AH0 S P EH1 K T} him"},
            {"Close the door, it was close.", "{K L OW1 Z} the door it was {K L OW1 S}"},
            {"On May 5th, may I?", "on {M EY1} fifth may i"},
            {"Dr. Smith lives on St. James St.", "doctor smith {L IH1 V Z} on saint james street"},
        };
        int bad = 0;
        for (auto& c : kNorm) {
            std::vector<Speech::detail::TextWord> tw;
            Speech::detail::normalizeText(c[0], tw);
            std::string got;
            for (auto& w : tw) {
                if (!got.empty()) got += ' ';
                got += w.phon ? "{" + w.w + "}" : w.w;
            }
            if (got != c[1]) {
                printf("  normalization mismatch: '%s'\n    got:      %s\n    expected: %s\n", c[0], got.c_str(), c[1]);
                bad++;
            }
        }
        printf("Normalization checks: %s\n", bad ? "FAILED" : "OK");
        st.problems += bad;
    }
    // Edge cases.
    {
        std::vector<float> x;
        Speech::synthesize("", voices[0].v, 22050, x);
        Speech::synthesize("   ...  ", voices[0].v, 22050, x);
        Speech::synthesize("!!!", voices[0].v, 22050, x);
        size_t n0 = x.size();
        Speech::synthesize("Hmm.", voices[0].v, 22050, x);
        Speech::synthesize("GET DOWN! NOW!", voices[0].v, 48000, x);
        Speech::synthesize("Call 555-0123 or 911, it's 2:45 pm on Jan. 3rd, 1987. That's 50% off $2.5 million!", voices[1].v, 32000, x);
        Speech::synthesize("Ünïcödé café naïve — “quotes” and ‘apostrophes’…", voices[2].v, 22050, x);
        Speech::synthesize("{HH AH0 L OW1} *really* SWAT FBI LAPD K9 AK-47 nothin' y'all", voices[0].v, 22050, x);
        bool finite = true;
        for (float v : x) finite = finite && v == v && fabsf(v) <= 0.81f;
        printf("Edge cases: empty-ish produced %zu samples, others %zu samples, finite=%s\n", n0, x.size() - n0,
               finite ? "yes" : "NO");
        if (!finite) st.problems++;
        for (int r : {22050, 32000, 44100, 48000}) {
            std::vector<float> y;
            Speech::synthesize("Rate test.", voices[1].v, r, y);
            float pk = 0;
            for (float v : y) pk = std::max(pk, fabsf(v));
            printf("  rate %d: %zu samples, peak %.3f\n", r, y.size(), pk);
        }
    }
    printf("Problems: %d\n", st.problems);
    return st.problems ? 1 : 0;
}
