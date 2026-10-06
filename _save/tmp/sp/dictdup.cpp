#include <cstdarg>
#include <map>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
int main() {
    size_t n = sizeof(Speech::detail::kDictEntries) / sizeof(Speech::detail::kDictEntries[0]);
    std::map<std::string, std::vector<std::string>> u;
    for (size_t i = 0; i < n; i++) {
        std::string e = Speech::detail::kDictEntries[i];
        size_t sp = e.find(' ');
        u[e.substr(0, sp)].push_back(e.substr(sp + 1));
    }
    for (auto& kv : u) {
        bool diff = false;
        for (auto& p : kv.second) if (p != kv.second[0]) diff = true;
        if (diff) { printf("%-14s", kv.first.c_str()); for (auto& p : kv.second) printf(" | %s", p.c_str()); printf("   => used: %s\n", Speech::detail::dictLookup(kv.first)); }
    }
}
