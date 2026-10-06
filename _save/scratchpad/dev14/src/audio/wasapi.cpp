// Output backend. Windows: WASAPI shared mode, event driven, on a dedicated "Pro Audio" (MMCSS)
// thread. Uses the device mix format (any rate / channel count / float or PCM), resampling the
// 48 kHz stereo mix with a polyphase windowed-sinc filter when needed. Follows default-device changes
// and recovers from device loss (keeps the mixer running in real time and retries periodically).
// Other platforms: null backend (no device; the engine is still usable through renderOffline()).
#include "audio_internal.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>

namespace Audio {
namespace detail {
namespace wasapi {

static const GUID kCLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID kIID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID kIID_IAudioClient = {0x1CB9AD4C, 0xDBFA, 0x4C32, {0xB1, 0x78, 0xC2, 0xF5, 0x68, 0xA7, 0x03, 0xB2}};
static const GUID kIID_IAudioRenderClient = {0xF294ACFC, 0x3146, 0x4483, {0xA7, 0xBF, 0xAD, 0xDC, 0xA7, 0xC2, 0x60, 0xE2}};
static const GUID kIID_IMMNotificationClient = {0x7991EEC9, 0x7E89, 0x4D85, {0x83, 0x90, 0x6C, 0x70, 0x3C, 0xEC, 0x60, 0xC0}};
static const GUID kIID_IUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
static const GUID kSubtypePcm = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

static bool guidEq(const GUID& a, const GUID& b) { return memcmp(&a, &b, sizeof(GUID)) == 0; }

class NotifyClient : public IMMNotificationClient {
public:
    std::atomic<bool> defaultChanged{false};
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (guidEq(riid, kIID_IUnknown) || guidEq(riid, kIID_IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) defaultChanged.store(true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
};

// Polyphase windowed-sinc resampler: 48 kHz stereo -> device rate.
struct Resampler {
    static constexpr int kTaps = 32;
    static constexpr int kPhases = 256;
    std::vector<float> table;  // (kPhases + 1) * kTaps
    double ratio = 1.0;        // input samples per output sample
    double pos = 0.0;          // read position into the input history
    std::vector<float> inL, inR;
    int inLen = 0;
    bool bypass = true;
    void init(int outRate) {
        bypass = outRate == kSampleRate;
        ratio = (double)kSampleRate / (double)outRate;
        double cutoff = Min(1.0, 1.0 / ratio) * 0.94;
        table.assign((size_t)(kPhases + 1) * kTaps, 0.f);
        for (int p = 0; p <= kPhases; p++) {
            double frac = (double)p / kPhases;
            double sum = 0.0;
            for (int k = 0; k < kTaps; k++) {
                double x = (double)(k - kTaps / 2 + 1) - frac;
                double s = fabs(x) < 1e-9 ? cutoff : sin(3.14159265358979 * cutoff * x) / (3.14159265358979 * x);
                double w = 0.42 - 0.5 * cos(2.0 * 3.14159265358979 * ((double)k + 1.0 - frac) / (double)kTaps) +
                           0.08 * cos(4.0 * 3.14159265358979 * ((double)k + 1.0 - frac) / (double)kTaps);
                double v = s * Max(w, 0.0);
                table[(size_t)p * kTaps + (size_t)k] = (float)v;
                sum += v;
            }
            for (int k = 0; k < kTaps; k++) table[(size_t)p * kTaps + (size_t)k] = (float)(table[(size_t)p * kTaps + (size_t)k] / sum);
        }
        inL.assign(8192, 0.f);
        inR.assign(8192, 0.f);
        inLen = kTaps;
        pos = (double)(kTaps / 2);
    }
    // Produces `frames` output frames (interleaved stereo) pulling input from cb.
    void process(float* out, int frames, RenderCallback cb) {
        float tmp[kMaxBlock * 2];
        for (int o = 0; o < frames; o++) {
            int i0 = (int)pos;
            while (i0 + kTaps / 2 + 1 >= inLen) {
                // compact consumed input
                int keep = i0 - kTaps / 2;
                if (keep > 0) {
                    memmove(inL.data(), inL.data() + keep, sizeof(float) * (size_t)(inLen - keep));
                    memmove(inR.data(), inR.data() + keep, sizeof(float) * (size_t)(inLen - keep));
                    inLen -= keep;
                    pos -= (double)keep;
                    i0 -= keep;
                }
                int n = Min(kMaxBlock, (int)inL.size() - inLen);
                cb(tmp, n);
                for (int k = 0; k < n; k++) {
                    inL[(size_t)(inLen + k)] = tmp[k * 2];
                    inR[(size_t)(inLen + k)] = tmp[k * 2 + 1];
                }
                inLen += n;
            }
            double frac = pos - (double)i0;
            double ph = frac * kPhases;
            int p0 = (int)ph;
            float pf = (float)(ph - (double)p0);
            const float* h0 = &table[(size_t)p0 * kTaps];
            const float* h1 = &table[(size_t)(p0 + 1) * kTaps];
            float l = 0.f, r = 0.f;
            int base = i0 - kTaps / 2 + 1;
            for (int k = 0; k < kTaps; k++) {
                float h = h0[k] + (h1[k] - h0[k]) * pf;
                l += inL[(size_t)(base + k)] * h;
                r += inR[(size_t)(base + k)] * h;
            }
            out[o * 2] = l;
            out[o * 2 + 1] = r;
            pos += ratio;
        }
    }
};

enum class SampleFmt { F32, S16, S24, S32 };

struct Device {
    IMMDevice* dev = nullptr;
    IAudioClient* client = nullptr;
    IAudioRenderClient* render = nullptr;
    HANDLE event = nullptr;
    WAVEFORMATEX* mix = nullptr;
    UINT32 bufferFrames = 0;
    int channels = 2;
    int rate = 48000;
    SampleFmt fmt = SampleFmt::F32;
    int bytesPerSample = 4;
    Resampler rs;
    std::vector<float> stereo;
};

static void closeDevice(Device& d) {
    if (d.client) d.client->Stop();
    if (d.render) { d.render->Release(); d.render = nullptr; }
    if (d.client) { d.client->Release(); d.client = nullptr; }
    if (d.dev) { d.dev->Release(); d.dev = nullptr; }
    if (d.mix) { CoTaskMemFree(d.mix); d.mix = nullptr; }
    if (d.event) { CloseHandle(d.event); d.event = nullptr; }
}

static bool openDevice(IMMDeviceEnumerator* en, Device& d) {
    closeDevice(d);
    if (!en) return false;
    HRESULT hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &d.dev);
    if (FAILED(hr) || !d.dev) return false;
    hr = d.dev->Activate(kIID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&d.client);
    if (FAILED(hr) || !d.client) { closeDevice(d); return false; }
    hr = d.client->GetMixFormat(&d.mix);
    if (FAILED(hr) || !d.mix) { closeDevice(d); return false; }
    d.channels = d.mix->nChannels;
    d.rate = (int)d.mix->nSamplesPerSec;
    int bits = d.mix->wBitsPerSample;
    bool isFloat = false;
    if (d.mix->wFormatTag == WAVE_FORMAT_EXTENSIBLE && d.mix->cbSize >= 22) {
        const WAVEFORMATEXTENSIBLE* ext = (const WAVEFORMATEXTENSIBLE*)d.mix;
        if (guidEq(ext->SubFormat, kSubtypeFloat)) isFloat = true;
        else if (!guidEq(ext->SubFormat, kSubtypePcm)) { closeDevice(d); return false; }
    } else if (d.mix->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        isFloat = true;
    } else if (d.mix->wFormatTag != WAVE_FORMAT_PCM) {
        closeDevice(d);
        return false;
    }
    if (isFloat && bits == 32) d.fmt = SampleFmt::F32;
    else if (!isFloat && bits == 16) d.fmt = SampleFmt::S16;
    else if (!isFloat && bits == 24) d.fmt = SampleFmt::S24;
    else if (!isFloat && bits == 32) d.fmt = SampleFmt::S32;
    else { closeDevice(d); return false; }
    d.bytesPerSample = bits / 8;
    if (d.channels < 1 || d.rate < 8000) { closeDevice(d); return false; }
    REFERENCE_TIME dur = 200000;  // 20 ms
    hr = d.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, dur, 0, d.mix, nullptr);
    if (FAILED(hr)) { closeDevice(d); return false; }
    d.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!d.event) { closeDevice(d); return false; }
    hr = d.client->SetEventHandle(d.event);
    if (FAILED(hr)) { closeDevice(d); return false; }
    hr = d.client->GetBufferSize(&d.bufferFrames);
    if (FAILED(hr) || d.bufferFrames == 0) { closeDevice(d); return false; }
    hr = d.client->GetService(kIID_IAudioRenderClient, (void**)&d.render);
    if (FAILED(hr) || !d.render) { closeDevice(d); return false; }
    BYTE* data = nullptr;
    if (SUCCEEDED(d.render->GetBuffer(d.bufferFrames, &data))) d.render->ReleaseBuffer(d.bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
    d.rs.init(d.rate);
    d.stereo.assign((size_t)d.bufferFrames * 2 + 16, 0.f);
    hr = d.client->Start();
    if (FAILED(hr)) { closeDevice(d); return false; }
    LOG("Audio: WASAPI %d Hz, %d ch, %d-bit %s, buffer %u frames", d.rate, d.channels, bits, isFloat ? "float" : "pcm", (unsigned)d.bufferFrames);
    return true;
}

static void writeFrames(Device& d, BYTE* dst, UINT32 frames, RenderCallback cb) {
    if (d.stereo.size() < (size_t)frames * 2) d.stereo.resize((size_t)frames * 2);
    float* s = d.stereo.data();
    if (d.rs.bypass) {
        UINT32 done = 0;
        while (done < frames) {
            int n = Min(kMaxBlock, (int)(frames - done));
            cb(s + done * 2, n);
            done += (UINT32)n;
        }
    } else {
        d.rs.process(s, (int)frames, cb);
    }
    int ch = d.channels;
    for (UINT32 i = 0; i < frames; i++) {
        float l = s[i * 2], r = s[i * 2 + 1];
        for (int c = 0; c < ch; c++) {
            float v = ch == 1 ? 0.5f * (l + r) : (c == 0 ? l : (c == 1 ? r : 0.f));
            v = Clamp(v, -1.f, 1.f);
            size_t idx = ((size_t)i * (size_t)ch + (size_t)c);
            switch (d.fmt) {
                case SampleFmt::F32: ((float*)dst)[idx] = v; break;
                case SampleFmt::S16: ((i16*)dst)[idx] = (i16)lrintf(v * 32767.f); break;
                case SampleFmt::S24: {
                    i32 q = (i32)lrintf(v * 8388607.f);
                    BYTE* p = dst + idx * 3;
                    p[0] = (BYTE)(q & 0xFF);
                    p[1] = (BYTE)((q >> 8) & 0xFF);
                    p[2] = (BYTE)((q >> 16) & 0xFF);
                    break;
                }
                case SampleFmt::S32: ((i32*)dst)[idx] = (i32)llrint((double)v * 2147483647.0); break;
            }
        }
    }
}

struct State {
    std::thread thread;
    std::atomic<bool> quit{false};
    std::atomic<bool> running{false};
    std::mutex m;
    std::condition_variable cv;
    bool initDone = false;
    bool initOk = false;
    RenderCallback cb = nullptr;
    HANDLE wake = nullptr;
};
static State g_state;

static double nowSec() {
    static LARGE_INTEGER freq = {};
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

static void threadMain() {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    typedef HANDLE(WINAPI * AvSetFn)(LPCSTR, LPDWORD);
    typedef BOOL(WINAPI * AvRevertFn)(HANDLE);
    HMODULE avrt = LoadLibraryA("avrt.dll");
    AvSetFn avSet = avrt ? (AvSetFn)(void*)GetProcAddress(avrt, "AvSetMmThreadCharacteristicsA") : nullptr;
    AvRevertFn avRevert = avrt ? (AvRevertFn)(void*)GetProcAddress(avrt, "AvRevertMmThreadCharacteristics") : nullptr;
    DWORD taskIdx = 0;
    HANDLE task = avSet ? avSet("Pro Audio", &taskIdx) : nullptr;
    if (!task) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    IMMDeviceEnumerator* en = nullptr;
    HRESULT hr = CoCreateInstance(kCLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL, kIID_IMMDeviceEnumerator, (void**)&en);
    NotifyClient notify;
    bool notifyRegistered = false;
    if (SUCCEEDED(hr) && en) notifyRegistered = SUCCEEDED(en->RegisterEndpointNotificationCallback(&notify));
    Device dev;
    bool ok = SUCCEEDED(hr) && en && openDevice(en, dev);
    {
        std::lock_guard<std::mutex> lk(g_state.m);
        g_state.initDone = true;
        g_state.initOk = ok;
    }
    g_state.cv.notify_all();
    if (ok) {
        bool lost = false;
        double lastRetry = nowSec(), lastTick = nowSec();
        std::vector<float> scratch((size_t)kMaxBlock * 2);
        while (!g_state.quit.load()) {
            if (!lost) {
                HANDLE hs[2] = {dev.event, g_state.wake};
                WaitForMultipleObjects(2, hs, FALSE, 200);
                if (g_state.quit.load()) break;
                if (notify.defaultChanged.exchange(false)) {
                    LOG("Audio: default output device changed - reopening");
                    lost = !openDevice(en, dev);
                    lastRetry = lastTick = nowSec();
                    continue;
                }
                UINT32 padding = 0;
                hr = dev.client->GetCurrentPadding(&padding);
                if (FAILED(hr)) {
                    LOG("Audio: device lost (0x%08x)", (unsigned)hr);
                    closeDevice(dev);
                    lost = true;
                    lastRetry = lastTick = nowSec();
                    continue;
                }
                UINT32 avail = dev.bufferFrames > padding ? dev.bufferFrames - padding : 0;
                if (avail == 0) continue;
                BYTE* data = nullptr;
                hr = dev.render->GetBuffer(avail, &data);
                if (FAILED(hr)) {
                    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                        LOG("Audio: device invalidated");
                        closeDevice(dev);
                        lost = true;
                        lastRetry = lastTick = nowSec();
                    }
                    continue;
                }
                writeFrames(dev, data, avail, g_state.cb);
                dev.render->ReleaseBuffer(avail, 0);
            } else {
                // No device: keep the game's audio state advancing in real time, retry periodically.
                WaitForSingleObject(g_state.wake, 10);
                if (g_state.quit.load()) break;
                double now = nowSec();
                int frames = Clamp((int)((now - lastTick) * kSR), 0, 4800);
                lastTick += (double)frames / kSR;
                while (frames > 0) {
                    int n = Min(frames, kMaxBlock);
                    g_state.cb(scratch.data(), n);
                    frames -= n;
                }
                if (now - lastRetry > 1.5) {
                    lastRetry = now;
                    notify.defaultChanged.store(false);
                    if (openDevice(en, dev)) {
                        lost = false;
                        LOG("Audio: output device recovered");
                    }
                }
            }
        }
    }
    closeDevice(dev);
    if (en) {
        if (notifyRegistered) en->UnregisterEndpointNotificationCallback(&notify);
        en->Release();
    }
    if (task && avRevert) avRevert(task);
    if (avrt) FreeLibrary(avrt);
    if (SUCCEEDED(hrCo)) CoUninitialize();
    g_state.running.store(false);
}

}  // namespace wasapi

namespace backend {
bool start(RenderCallback cb) {
    using namespace wasapi;
    if (g_state.running.load()) return true;
    g_state.cb = cb;
    g_state.quit.store(false);
    g_state.initDone = false;
    g_state.initOk = false;
    if (!g_state.wake) g_state.wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_state.running.store(true);
    g_state.thread = std::thread(threadMain);
    bool ok;
    {
        std::unique_lock<std::mutex> lk(g_state.m);
        g_state.cv.wait_for(lk, std::chrono::seconds(4), [] { return g_state.initDone; });
        ok = g_state.initDone && g_state.initOk;
    }
    if (!ok) stop();
    return ok;
}
void stop() {
    using namespace wasapi;
    g_state.quit.store(true);
    if (g_state.wake) SetEvent(g_state.wake);
    if (g_state.thread.joinable()) g_state.thread.join();
    g_state.running.store(false);
}
bool running() { return wasapi::g_state.running.load(); }
void setThreadHighPriority() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL); }
void setThreadLowPriority() { SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL); }
}  // namespace backend

}  // namespace detail
}  // namespace Audio

#else  // !_WIN32: null backend

namespace Audio {
namespace detail {
namespace backend {
bool start(RenderCallback) { return false; }
void stop() {}
bool running() { return false; }
void setThreadHighPriority() {}
void setThreadLowPriority() {}
}  // namespace backend
}  // namespace detail
}  // namespace Audio

#endif
