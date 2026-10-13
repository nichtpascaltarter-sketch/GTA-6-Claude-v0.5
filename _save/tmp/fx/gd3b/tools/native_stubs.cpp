// Definitions of platform services for native (Linux) tool/test builds of engine modules.
#include "../src/core/base.h"
#include <cstdarg>
#include <chrono>
void LogPrintf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stdout, fmt, ap); va_end(ap); fputc('\n', stdout); fflush(stdout);
}
void FatalError(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap); fputc('\n', stderr); exit(1);
}
std::string StrFormat(const char* fmt, ...) {
    char buf[4096]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap); return buf;
}
double TimeSeconds() {
    static auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}
