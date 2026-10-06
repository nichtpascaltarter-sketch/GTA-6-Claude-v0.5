#include <cstdarg>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
int main() {
    size_t n = sizeof(Speech::detail::kDictEntries) / sizeof(Speech::detail::kDictEntries[0]);
    std::unordered_map<std::string, int> u;
    int bad = 0;
    for (size_t i = 0; i < n; i++) {
        std::string e = Speech::detail::kDictEntries[i];
        size_t sp = e.find(' ');
        u[e.substr(0, sp)]++;
        Speech::detail::Pron p;
        if (!Speech::detail::parsePhonemes(e.c_str() + sp + 1, p)) { bad++; printf("bad: %s\n", e.c_str()); }
    }
    int dups = 0;
    for (auto& kv : u) if (kv.second > 1) dups++;
    printf("entries %zu unique %zu duplicated-words %d parse-errors %d\n", n, u.size(), dups, bad);
}
