#include <cstdarg>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
int main(int argc, char** argv) {
    for (int a = 1; a < argc; a++) {
        std::vector<Speech::detail::TextWord> tw;
        Speech::detail::normalizeText(argv[a], tw);
        std::string s;
        for (auto& w : tw) { if (!s.empty()) s += ' '; s += w.phon ? "{" + w.w + "}" : w.w; }
        printf("%s\n", s.c_str());
    }
}
