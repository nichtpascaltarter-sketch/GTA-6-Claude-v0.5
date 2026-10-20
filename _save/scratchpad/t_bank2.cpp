#include <cstdarg>
#include <chrono>
#include "audio/audio_internal.h"
#include "audio/emitters.cpp"
#include "audio/sfx.cpp"
void LogPrintf(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
void FatalError(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); exit(1); }
std::string StrFormat(const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); return b; }
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
int main() {
    struct R { double t; int id; double secs; };
    std::vector<R> rs;
    for (int i = 1; i < Audio::detail::BANK_COUNT; i++) {
        double t0 = TimeSeconds();
        Audio::detail::bankRenderEntry(i);
        double t1 = TimeSeconds();
        auto& e = Audio::detail::bankEntry(i);
        double secs = 0; for (int v = 0; v < e.count; v++) secs += e.vars[v].frames / 48000.0;
        rs.push_back({t1 - t0, i, secs});
    }
    std::sort(rs.begin(), rs.end(), [](const R& a, const R& b){ return a.t > b.t; });
    double tt = 0, ts = 0; for (auto& r : rs) { tt += r.t; ts += r.secs; }
    printf("total %.3f s cpu, %.1f s audio\n", tt, ts);
    for (int i = 0; i < 30; i++) printf("%-20s %.3f s  audio %.2f s (%d vars)\n", Audio::detail::soundDef(rs[i].id).name, rs[i].t, rs[i].secs, Audio::detail::bankEntry(rs[i].id).count);
    std::sort(rs.begin(), rs.end(), [](const R& a, const R& b){ return a.secs > b.secs; });
    printf("--- by length\n");
    for (int i = 0; i < 25; i++) printf("%-20s audio %.2f s\n", Audio::detail::soundDef(rs[i].id).name, rs[i].secs);
}
