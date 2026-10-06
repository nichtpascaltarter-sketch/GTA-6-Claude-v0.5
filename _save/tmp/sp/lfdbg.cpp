#include <cstdarg>
#include <chrono>
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
[[noreturn]] void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
using namespace Speech::detail;
int main() {
    for (float rd = 0.3f; rd <= 2.71f; rd += 0.1f) {
        LFShape s = lfCompute(rd);
        // numeric area check
        int N = 20000; double U = 0, minU = 1e9, maxE = -1e9, minE = 1e9;
        double sum = 0;
        for (int i = 0; i < N; i++) {
            double t = (i + 0.5) / N, E;
            if (t < s.te) E = s.E0 * exp(s.alpha * t) * sin(s.wg * t);
            else E = -(1.0 / (s.eps * s.ta)) * (exp(-s.eps * (t - s.te)) - exp(-s.eps * (1 - s.te)));
            sum += E / N; maxE = std::max(maxE, E); minE = std::min(minE, E);
        }
        printf("rd %.2f tp %.3f te %.3f ta %.4f eps %.1f alpha %.2f E0 %.3f Ue %.4f Upk %.4f area %.2e Emax %.2f Emin %.2f\n",
               rd, s.tp, s.te, s.ta, s.eps, s.alpha, s.E0, s.Ue, s.Upeak, sum, maxE, minE);
    }
}
