#include <cstdarg>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
int main() {
    const char* t = "[calm]Relax. It's fine. [angry]I said RELAX! [laughs] [happy]Just kidding.";
    Audio::VoiceParams v = Speech::persona("dex").voice;
    std::vector<Speech::StyleSpan> sp; Speech::styleTimeline(t, v, sp);
    for (auto& x : sp) printf("span %.2f-%.2f emotion %d intensity %.1f\n", x.start, x.end, x.style.emotion, x.style.intensity);
    std::vector<Speech::AccentCue> c; Speech::accentCues(t, v, c);
    for (auto& x : c) printf("accent %.2f s strength %.2f %s\n", x.time, x.strength, x.nuclear ? "nuclear" : "");
    printf("duration %.2f\n", Speech::estimateDuration(t, v));
}
