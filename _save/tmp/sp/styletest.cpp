#include <cstdarg>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
static void wav(const std::string& path, const std::vector<float>& x, int sr) {
    FILE* f = fopen(path.c_str(), "wb"); u32 n = (u32)x.size() * 2;
    fwrite("RIFF", 1, 4, f); u32 v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); u16 h = 1; fwrite(&h, 2, 1, f); fwrite(&h, 2, 1, f); v = sr; fwrite(&v, 4, 1, f);
    v = sr * 2; fwrite(&v, 4, 1, f); h = 2; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f); fwrite("data", 1, 4, f);
    fwrite(&n, 4, 1, f); for (float s : x) { i16 q = (i16)lrintf(std::max(-1.f, std::min(1.f, s)) * 32767); fwrite(&q, 2, 1, f); }
    fclose(f);
}
int main(int argc, char** argv) {
    std::string out = argc > 1 ? argv[1] : "/tmp/sp/styles";
    const char* texts[] = {"Get in the car, we have to go now! Is that the police?",
                           "I told you, the package has to be there by midnight.",
                           "Suspect is heading north on the highway in a red sedan."};
    const char* tags[] = {"", "[angry]", "[scared]", "[calm]", "[sad]", "[happy]", "[shout]", "[whisper]", "[dj]", "[ad]",
                          "[fineprint]", "[news]", "[dispatch]", "[accent:south]", "[accent:newyork]", "[accent:latino]",
                          "[accent:caribbean]", "[accent:british]"};
    Audio::VoiceParams m = Speech::persona("dex").voice, f = Speech::persona("mari").voice;
    FILE* man = fopen((out + "/manifest_focus.tsv").c_str(), "w");
    for (int ti = 0; ti < 3; ti++) {
    const char* text = texts[ti];
    for (const char* tg : tags) {
        for (int g = 0; g < 2; g++) {
            std::string t = std::string(tg) + text;
            std::vector<float> x;
            Speech::synthesize(t.c_str(), g ? f : m, 22050, x);
            float pk = 0; double s2 = 0; bool fin = true;
            for (float v : x) { fin = fin && v == v; pk = std::max(pk, fabsf(v)); s2 += v * v; }
            float est = Speech::estimateDuration(t.c_str(), g ? f : m);
            std::vector<Speech::VisemeKey> vk; Speech::lipSync(t.c_str(), g ? f : m, vk);
            float vend = vk.empty() ? 0 : vk.back().time + vk.back().duration;
            std::string name = std::string(*tg ? std::string(tg).substr(1, strlen(tg) - 2) : std::string("plain"));
            for (char& c : name) if (c == ':') c = '_';
            name += g ? "_F" : "_M";
            name += std::to_string(ti);
            printf("%-22s dur %.2f est %.2f visEnd %.2f keys %3zu peak %.2f rms %.3f %s\n", name.c_str(), x.size() / 22050.0, est, vend, vk.size(), pk, sqrt(s2 / x.size()), fin ? "" : "NAN!");
            wav(out + "/" + name + ".wav", x, 22050);
            std::string vname = name.substr(0, name.size() - 3);
            fprintf(man, "%s.wav\t%s\t%s\n", name.c_str(), vname.c_str(), Speech::displayText(t.c_str()).c_str());
        }
    }
    }
    fclose(man);
}
