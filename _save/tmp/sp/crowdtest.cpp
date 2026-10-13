#include <cstdarg>
#include "audio/audio_all.cpp"
#include "audio/speech.cpp"
void LogPrintf(const char*, ...) {}
void FatalError(const char*, ...) { exit(1); }
std::string StrFormat(const char*, ...) { return ""; }
double TimeSeconds() { return 0; }
using namespace Audio;
static double rmsOf(const std::vector<float>& b, size_t from) {
    double s = 0; size_t n = 0; for (size_t i = from; i < b.size(); i++) { s += (double)b[i] * b[i]; n++; }
    return sqrt(s / std::max<size_t>(1, n));
}
static double hfRatio(const std::vector<float>& b) {  // energy above ~3 kHz vs total (first-difference proxy)
    double d = 0, s = 0; for (size_t i = 2; i < b.size(); i += 2) { double x = b[i] - b[i - 2]; d += x * x; s += (double)b[i] * b[i]; }
    return d / std::max(1e-12, s);
}
int main() {
    Ambience a; a.urban = 0.f; a.nature = 0.f; a.coast = 0.f; a.wind = 0.f; a.rain = 0.f;
    setAmbience(a);
    const int frames = 48000 * 3;
    std::vector<float> buf(frames * 2);
    Listener l; l.pos = vec3(0, 0, 0);
    update(l, 0.016f);
    renderOffline(buf.data(), frames);
    printf("silence   rms %.4f\n", rmsOf(buf, 0));
    for (int place = 0; place < CROWD_PLACE_COUNT; place++) {
        setCrowd(0.8f, place);
        renderOffline(buf.data(), frames);  // crossfade in
        renderOffline(buf.data(), frames);
        bool fin = true; float pk = 0; for (float v : buf) { fin = fin && std::isfinite(v); pk = std::max(pk, fabsf(v)); }
        printf("place %d density 0.8 rms %.4f peak %.3f hf %.3f %s\n", place, rmsOf(buf, 0), pk, hfRatio(buf), fin ? "" : "NaN");
    }
    setCrowd(0.2f, CROWD_STREET);
    renderOffline(buf.data(), frames); renderOffline(buf.data(), frames);
    printf("street 0.2       rms %.4f\n", rmsOf(buf, 0));
    setCrowd(0.8f, CROWD_STREET);
    renderOffline(buf.data(), frames); renderOffline(buf.data(), frames);
    printf("street 0.8       rms %.4f hf %.3f\n", rmsOf(buf, 0), hfRatio(buf));
    l.interior = 1.f; update(l, 0.016f);
    renderOffline(buf.data(), frames); renderOffline(buf.data(), frames);
    printf("street indoors   rms %.4f hf %.3f\n", rmsOf(buf, 0), hfRatio(buf));
    l.interior = 0.f; update(l, 0.016f);
    setCrowd(0.6f, CROWD_STREET, 1.f);
    renderOffline(buf.data(), frames);
    { bool fin = true; float pk = 0; for (float v : buf) { fin = fin && std::isfinite(v); pk = std::max(pk, fabsf(v)); }
      printf("street panic     rms %.4f peak %.3f %s\n", rmsOf(buf, 0), pk, fin ? "" : "NaN"); }
    setCrowd(0.6f, CROWD_STREET, 0.f);
    renderOffline(buf.data(), frames); renderOffline(buf.data(), frames); renderOffline(buf.data(), frames);
    printf("after panic      rms %.4f\n", rmsOf(buf, 0));
    setCrowd(0.f, CROWD_STREET);
    renderOffline(buf.data(), frames); renderOffline(buf.data(), frames);
    printf("density 0        rms %.4f\n", rmsOf(buf, 0));
}
