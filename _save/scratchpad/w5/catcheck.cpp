#include <cstdarg>
#include <chrono>
#include <set>
#include "audio/audio_all.cpp"
#include "../tests/audio/speech_stub.cpp"
void LogPrintf(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
void FatalError(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); exit(1); }
std::string StrFormat(const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); return b; }
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
using namespace Audio::detail;
int main(int argc, char** argv) {
    radio::ensureStations();
    for (int i = 0; i < radio::kStations; i++) {
        auto& st = radio::g_st[i];
        std::set<std::string> titles, artists;
        int suffixed = 0;
        double total = 0;
        for (auto& si : st.catalog) {
            titles.insert(si.title);
            artists.insert(si.artist);
            if (si.title.find(" (") != std::string::npos) suffixed++;
            total += si.dur;
        }
        printf("%-28s songs %2zu unique titles %2zu artists %2zu suffixed %d  total %.0f min\n", st.def.name, st.catalog.size(), titles.size(), artists.size(), suffixed, total / 60.0);
        if (argc > 1 && atoi(argv[1]) == i)
            for (auto& si : st.catalog) printf("    %-28s %-36s %.0f s\n", si.artist.c_str(), si.title.c_str(), si.dur);
    }
}
