#include <cstdarg>
#include <fstream>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
int main(int argc, char** argv) {
    std::ifstream f(argv[1]); std::string line; int n = 0, bad = 0; double audio = 0;
    Audio::VoiceParams vs[3] = {Speech::presetVoice(false, 3), Speech::presetVoice(true, 8), Speech::persona("cop").voice};
    while (std::getline(f, line)) {
        for (auto& v : vs) {
            std::vector<float> x; Speech::synthesize(line.c_str(), v, 22050, x);
            float pk = 0; bool fin = true; for (float s : x) { fin = fin && s == s; pk = std::max(pk, fabsf(s)); }
            float est = Speech::estimateDuration(line.c_str(), v);
            if (!fin || pk > 0.81f || pk < 0.2f || fabsf(x.size() / 22050.f - est) > 0.01f) { bad++; printf("BAD %s\n", line.c_str()); }
            audio += x.size() / 22050.0; n++;
        }
        std::string d = Speech::displayText(line.c_str());
        if (d.find('[') != std::string::npos) printf("markup left: %s\n", d.c_str());
    }
    printf("%d renders, %.1f s audio, bad %d\n", n, audio, bad);
}
