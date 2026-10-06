// Dumps bank entries to WAV + band energy table (analysis helper, not shipped).
#include <cstdarg>
#include <chrono>
#include "audio/audio_all.cpp"
#include "speech_stub.cpp"
void LogPrintf(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); printf("\n"); }
void FatalError(const char* fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); exit(1); }
std::string StrFormat(const char* fmt, ...) { char b[1024]; va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a); return b; }
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
using namespace Audio::detail;
static void wav(const std::string& p, const SoundBuffer& b) {
    FILE* f = fopen(p.c_str(), "wb"); if (!f) return;
    int ch = b.channels, n = b.frames, db = n * ch * 2, riff = 36 + db, fl = 16, sr = 48000, br = sr * ch * 2; short fmt = 1, c = (short)ch, ba = (short)(ch * 2), bits = 16;
    fwrite("RIFF",1,4,f); fwrite(&riff,4,1,f); fwrite("WAVEfmt ",1,8,f); fwrite(&fl,4,1,f); fwrite(&fmt,2,1,f); fwrite(&c,2,1,f); fwrite(&sr,4,1,f); fwrite(&br,4,1,f); fwrite(&ba,2,1,f); fwrite(&bits,2,1,f);
    fwrite("data",1,4,f); fwrite(&db,4,1,f); fwrite(b.data.data(), 2, b.data.size(), f); fclose(f);
}
int main(int argc, char** argv) {
    int from = argc > 1 ? atoi(argv[1]) : GUN_NEAR, to = argc > 2 ? atoi(argv[2]) : GUN_CRACK;
    std::string out = argc > 3 ? argv[3] : ".";
    for (int id = from; id <= to; id++) {
        bankRenderEntry(id);
        const SoundBuffer& b = bankEntry(id).vars[0];
        // band energies via DFT on the whole buffer (zero-padded to pow2)
        int n = 1; while (n < b.frames) n <<= 1;
        std::vector<double> re(n, 0), im(n, 0);
        for (int i = 0; i < b.frames; i++) { double v = 0; for (int c = 0; c < b.channels; c++) v += b.data[(size_t)i * b.channels + c]; re[i] = v / b.channels / 32767.0; }
        for (int i = 1, j = 0; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); } }
        for (int len = 2; len <= n; len <<= 1) { double ang = -2 * M_PI / len; for (int i = 0; i < n; i += len) for (int k = 0; k < len / 2; k++) {
            double wr = cos(ang * k), wi = sin(ang * k); double vr = re[i+k+len/2]*wr - im[i+k+len/2]*wi, vi = re[i+k+len/2]*wi + im[i+k+len/2]*wr;
            re[i+k+len/2] = re[i+k] - vr; im[i+k+len/2] = im[i+k] - vi; re[i+k] += vr; im[i+k] += vi; } }
        const double edges[] = {0, 150, 400, 1000, 2500, 5000, 10000, 24000};
        double band[7] = {}, tot = 1e-20;
        for (int k = 0; k <= n / 2; k++) { double f = k * 48000.0 / n, p = re[k]*re[k] + im[k]*im[k]; tot += p; for (int bb = 0; bb < 7; bb++) if (f >= edges[bb] && f < edges[bb+1]) band[bb] += p; }
        printf("%-22s %5.2fs ch%d |", soundDef(id).name, b.frames / 48000.0, b.channels);
        for (int bb = 0; bb < 7; bb++) printf(" %5.1f", 100.0 * band[bb] / tot);
        printf("\n");
        wav(out + "/" + soundDef(id).name + ".wav", b);
    }
    printf("bands: <150 | 150-400 | 400-1k | 1-2.5k | 2.5-5k | 5-10k | >10k (%% energy)\n");
}
