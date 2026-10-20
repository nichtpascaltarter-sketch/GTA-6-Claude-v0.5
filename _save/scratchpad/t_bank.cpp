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
    double t0 = TimeSeconds();
    Audio::detail::bankStartAsync(4);
    Audio::detail::bankWaitAll();
    double t1 = TimeSeconds();
    size_t total = 0;
    for (int i = 1; i < Audio::detail::BANK_COUNT; i++) {
        auto& e = Audio::detail::bankEntry(i);
        for (int v = 0; v < e.count; v++) total += e.vars[v].data.size();
    }
    printf("bank render %.3f s, %.1f MB\n", t1 - t0, total * 2.0 / 1e6);
}
