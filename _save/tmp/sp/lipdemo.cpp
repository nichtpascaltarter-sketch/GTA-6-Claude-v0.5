#include <cstdarg>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
int main(int argc, char** argv) {
    const char* text = argc > 1 ? argv[1] : "Hello there, my friend. Bob's big boat.";
    Audio::VoiceParams v = Speech::persona("mari").voice;
    std::vector<Speech::VisemeKey> k;
    Speech::lipSync(text, v, k);
    for (auto& x : k) printf("%.3f +%.3f %-3s %.2f\n", x.time, x.duration, Speech::visemeName(x.viseme), x.weight);
    std::vector<Speech::PhonemeTiming> p;
    Speech::phonemeTiming(text, v, p);
    for (auto& x : p) printf("[%s %.3f w%d v%s] ", x.name, x.start, x.word, Speech::visemeName(x.viseme));
    printf("\n");
    std::vector<float> a; Speech::synthesize(text, v, 48000, a);
    printf("audio %.3f s, keys end %.3f s\n", a.size() / 48000.0, k.back().time + k.back().duration);
}
