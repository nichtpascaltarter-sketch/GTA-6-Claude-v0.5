#include <cstdarg>
#include <chrono>
#include <time.h>
#include "audio/audio_all.cpp"
#include "speech_stub.cpp"
void LogPrintf(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
void FatalError(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); exit(1); }
std::string StrFormat(const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); return b; }
double TimeSeconds() { timespec ts; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; }
using namespace Audio::detail;
int main() {
    Audio::dsp::ScopedFlushDenormals ftz;
    std::vector<std::pair<double, int>> t;
    double tot = 0; size_t bytes = 0;
    for (int id = 1; id < BANK_COUNT; id++) {
        double t0 = TimeSeconds();
        bankRenderEntry(id);
        double d = TimeSeconds() - t0;
        tot += d;
        for (int v = 0; v < bankEntry(id).count; v++) bytes += bankEntry(id).vars[v].data.size() * 2;
        t.push_back({d, id});
    }
    std::vector<std::pair<size_t, int>> mem;
    for (int id = 1; id < BANK_COUNT; id++) { size_t b = 0; for (int v = 0; v < bankEntry(id).count; v++) b += bankEntry(id).vars[v].data.size() * 2; mem.push_back({b, id}); }
    std::sort(mem.rbegin(), mem.rend());
    for (int i = 0; i < 30; i++) printf("  mem %-22s %.2f MB (%d vars, ch %d, %.1f s)\n", soundDef(mem[i].second).name, mem[i].first / 1048576.0, bankEntry(mem[i].second).count, bankEntry(mem[i].second).vars[0].channels, bankEntry(mem[i].second).vars[0].frames / 48000.0);
    std::sort(t.rbegin(), t.rend());
    printf("total CPU %.2f s, %.1f MB\n", tot, bytes / 1048576.0);
    for (int i = 0; i < 25; i++) printf("  %-22s %.3f s\n", soundDef(t[i].second).name, t[i].first);
}
