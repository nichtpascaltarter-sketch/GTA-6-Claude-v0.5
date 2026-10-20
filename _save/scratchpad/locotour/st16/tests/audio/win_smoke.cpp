// Windows smoke test for the WASAPI backend (run natively or under Wine):
//   x86_64-w64-mingw32-g++ -std=c++17 -O2 -I src tests/audio/win_smoke.cpp -o /tmp/win_smoke.exe -static -lole32 -luuid -lavrt
#include <cstdarg>
#include <chrono>
#include "audio/audio_all.cpp"
#include "audio/speech.cpp"
void LogPrintf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    printf("\n");
    fflush(stdout);
}
void FatalError(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
    exit(1);
}
std::string StrFormat(const char* fmt, ...) {
    char b[1024];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    return b;
}
double TimeSeconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// Resampler check: 1 kHz sine at 48 kHz -> device rate; measures frequency and residual distortion.
static double g_ph = 0.0;
static void sineCb(float* out, int frames) {
    for (int i = 0; i < frames; i++) {
        float v = 0.5f * sinf((float)g_ph);
        g_ph += 2.0 * 3.14159265358979 * 1000.0 / 48000.0;
        out[i * 2] = v;
        out[i * 2 + 1] = v;
    }
}
static void testResampler(int rate) {
    Audio::detail::wasapi::Resampler rs;
    rs.init(rate);
    g_ph = 0.0;
    int n = rate;  // one second
    std::vector<float> out((size_t)n * 2);
    rs.process(out.data(), n, sineCb);
    // fit a 1 kHz sinusoid (least squares) on the second half and measure the residual
    double sc = 0, cc = 0, ss = 0, sx = 0, cx = 0;
    int a0 = n / 2;
    for (int i = a0; i < n; i++) {
        double t = (double)i / rate, s_ = sin(2 * 3.14159265358979 * 1000.0 * t), c_ = cos(2 * 3.14159265358979 * 1000.0 * t);
        sx += out[(size_t)i * 2] * s_;
        cx += out[(size_t)i * 2] * c_;
        ss += s_ * s_;
        cc += c_ * c_;
        sc += s_ * c_;
    }
    double A = sx / ss, B = cx / cc;
    double res = 0, sig = 0;
    for (int i = a0; i < n; i++) {
        double t = (double)i / rate;
        double fit = A * sin(2 * 3.14159265358979 * 1000.0 * t) + B * cos(2 * 3.14159265358979 * 1000.0 * t);
        res += (out[(size_t)i * 2] - fit) * (out[(size_t)i * 2] - fit);
        sig += fit * fit;
    }
    printf("resampler 48000->%d: amplitude %.4f, residual %.1f dB\n", rate, sqrt(A * A + B * B), 10 * log10(res / sig + 1e-30));
}

int main() {
    testResampler(44100);
    testResampler(96000);
    testResampler(32000);
    double t0 = TimeSeconds();
    bool ok = Audio::init();
    printf("init -> %d (%.2f s)\n", (int)ok, TimeSeconds() - t0);
    Audio::Listener l;
    Audio::Ambience amb;
    amb.urban = 1.f;
    Audio::setAmbience(amb);
    Audio::setRadioStation(3);
    Audio::EmitterHandle e = Audio::createEmitter(Audio::EMIT_ENGINE);
    for (int f = 0; f < 240; f++) {  // ~4 s at 60 fps
        Audio::update(l, 1.f / 60.f);
        Audio::setEmitter(e, vec3(0, 5, 0), vec3(), (float)(f % 120) / 120.f, 0.8f, 0.5f, (float)Audio::ENGINE_V8, 1.f);
        if (f % 30 == 0) Audio::play(Audio::SFX_PISTOL, vec3(3, 10, 0));
        if (f == 60) Audio::play2D(Audio::SFX_UI_SELECT);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    printf("now playing: %s\n", Audio::radioNowPlaying(3).c_str());
    std::vector<float> buf(48000 * 2);
    Audio::renderOffline(buf.data(), 48000);
    float pk = 0;
    for (float v : buf) pk = Max(pk, fabsf(v));
    printf("offline render peak %.3f\n", pk);
    Audio::destroyEmitter(e);
    Audio::shutdown();
    printf("shutdown ok (%.2f s total)\n", TimeSeconds() - t0);
    return 0;
}
