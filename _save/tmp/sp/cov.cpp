#include <cstdarg>
#include <fstream>
#include <set>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
using namespace Speech::detail;
int main(int argc, char** argv) {
    std::ifstream f(argv[1]);
    std::string line;
    std::set<std::string> seen;
    int total = 0, indict = 0;
    while (std::getline(f, line)) {
        std::vector<TextWord> tw;
        normalizeText(line.c_str(), tw);
        for (auto& w : tw) {
            if (w.spell || w.phon) continue;
            total++;
            if (dictLookup(w.w)) { indict++; continue; }
            if (!seen.insert(w.w).second) continue;
            WordPron wp;
            lookupWord(w, wp);
            std::string s;
            for (auto& x : wp.ph) { s += phInfo(x.ph).name; if (isVowel(x.ph)) s += (char)('0' + x.stress); s += ' '; }
            printf("%-16s %s\n", w.w.c_str(), s.c_str());
        }
    }
    fprintf(stderr, "tokens %d in dict %d (%.1f%%)\n", total, indict, 100.0 * indict / total);
}
