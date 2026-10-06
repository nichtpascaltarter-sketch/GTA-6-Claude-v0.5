// GPU timestamps, screenshots and debug readbacks, and the self-test of the D3D12 features the layer exposes.
// Included from gfx.cpp.
namespace gfx {
namespace {

constexpr u32 kMaxTimers = 64;
ID3D12QueryHeap* g_timerHeap = nullptr;
Buffer g_timerReadback[kFramesInFlight];
struct TimerStat {
    std::string name;
    double ms = 0;
};
std::vector<TimerStat> g_timerStats;
std::vector<u32> g_timerStack;
int g_timersOn = -1;
double g_tsFreq = 0;

bool timersEnabled() {
    if (g_timersOn < 0) g_timersOn = Platform::hasArg("gputimers") ? 1 : 0;
    return g_timersOn == 1;
}

void ensureTimerHeap() {
    if (g_timerHeap) return;
    D3D12_QUERY_HEAP_DESC qd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, kMaxTimers * 2 * kFramesInFlight, 0};
    checkHR(g.dev->CreateQueryHeap(&qd, __uuidof(ID3D12QueryHeap), (void**)&g_timerHeap), "CreateQueryHeap");
    for (auto& b : g_timerReadback) b = createBuffer(kMaxTimers * 2 * 8, 8, BUF_READBACK, nullptr, "timestamps");
    UINT64 f = 0;
    if (SUCCEEDED(g.queue[QUEUE_DIRECT]->GetTimestampFrequency(&f))) g_tsFreq = (double)f;
}

u32 timerBase() { return (u32)(g.frame % kFramesInFlight) * kMaxTimers * 2; }

float halfToFloat(u16 h) {
    u32 sign = (h >> 15) & 1, exp = (h >> 10) & 31, man = h & 1023;
    float v;
    if (exp == 0) v = ldexpf((float)man, -24);
    else if (exp == 31) v = man ? NAN : INFINITY;
    else v = ldexpf((float)(man | 1024), (int)exp - 25);
    return sign ? -v : v;
}

// Copies one texture subresource into a new readback buffer and waits for it.
bool readSubresource(ResourceObj* r, u32 sub, Buffer& rb, D3D12_PLACED_SUBRESOURCE_FOOTPRINT& fp) {
    UINT rows = 0;
    UINT64 rowBytes = 0, total = 0;
    g.dev->GetCopyableFootprints(&r->desc, sub, 1, 0, &fp, &rows, &rowBytes, &total);
    rb = createBuffer((u32)total, 0, BUF_READBACK, nullptr, "readback");
    ctx->transition(r, D3D12_RESOURCE_STATE_COPY_SOURCE, sub);
    ctx->flushBarriers();
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = rb.buf->d3d;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint = fp;
    dst.PlacedFootprint.Offset = 0;
    src.pResource = r->d3d;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.SubresourceIndex = sub;
    ctx->list()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    ctx->anyWork = true;
    waitIdle();
    return mapReadback(rb) != nullptr;
}

}  // namespace

void gpuTimerBegin(const char* name) {
    // Debugging aids at pass boundaries (off by default):
    // --gfxsync runs everything recorded so far to completion and logs the pass that starts, so a GPU fault or hang
    // points at the pass whose commands were executing;
    // --gfxsplit starts a new command list before every pass (--gfxsplit=a,b: before the named passes only), which
    // tells hazards that only an ExecuteCommandLists boundary resolves from ordinary pass bugs.
    static int mode = -1;
    static std::string splitList;
    if (mode < 0) {
        mode = Platform::hasArg("gfxsync") ? 2 : 0;
        if (!mode && Platform::hasArg("gfxsplit")) {
            mode = 1;
            const char* a = Platform::argValue("gfxsplit");
            if (a && a[0] != '-') splitList = std::string(",") + a + ",";   // (a following "--option" is not a list)
        }
    }
    if (mode == 2) {
        waitIdle();
        LOG("gfxsync: frame %llu, pass '%s'", (unsigned long long)g.frame, name);
    } else if (mode == 1 && ctx->anyWork && (splitList.empty() || splitList.find(std::string(",") + name + ",") != std::string::npos))
        ctx->submit();
    if (!timersEnabled()) return;
    ensureTimerHeap();
    FrameData& f = curFrame();
    if (f.timerCount >= kMaxTimers || f.timersResolved) return;
    u32 idx = f.timerCount++;
    if (f.timerNames.size() <= idx) f.timerNames.resize(idx + 1);
    f.timerNames[idx] = name;
    g_timerStack.push_back(idx);
    ctx->list()->EndQuery(g_timerHeap, D3D12_QUERY_TYPE_TIMESTAMP, timerBase() + idx * 2);
}

void gpuTimerEnd() {
    if (!timersEnabled() || g_timerStack.empty()) return;
    u32 idx = g_timerStack.back();
    g_timerStack.pop_back();
    ctx->list()->EndQuery(g_timerHeap, D3D12_QUERY_TYPE_TIMESTAMP, timerBase() + idx * 2 + 1);
}

void gpuTimersResolve() {
    if (!timersEnabled() || !g_timerHeap) return;
    FrameData& f = curFrame();
    if (f.timersResolved || !f.timerCount) return;
    while (!g_timerStack.empty()) gpuTimerEnd();
    u32 slot = (u32)(g.frame % kFramesInFlight);
    ctx->list()->ResolveQueryData(g_timerHeap, D3D12_QUERY_TYPE_TIMESTAMP, timerBase(), f.timerCount * 2, g_timerReadback[slot].buf->d3d, 0);
    f.timersResolved = true;
}

void timersFrameEnd(FrameData& f) {
    if (&f == &curFrame()) gpuTimersResolve();
}

void timersFrameRetired(FrameData& f) {
    if (f.timersResolved && f.timerCount) {
        u32 slot = (u32)(f.number % kFramesInFlight);
        const u64* ts = (const u64*)mapReadback(g_timerReadback[slot]);
        if (ts && g_tsFreq > 0) {
            if (g_timerStats.size() < f.timerCount) g_timerStats.resize(f.timerCount);
            for (u32 i = 0; i < f.timerCount; i++) {
                TimerStat& t = g_timerStats[i];
                t.name = f.timerNames[i];
                if (ts[i * 2 + 1] >= ts[i * 2]) {
                    double ms = (double)(ts[i * 2 + 1] - ts[i * 2]) / g_tsFreq * 1000.0;
                    t.ms = t.ms <= 0 ? ms : t.ms * 0.9 + ms * 0.1;
                }
            }
        }
    }
    f.timerCount = 0;
    f.timersResolved = false;
}

std::string gpuTimerReport() {
    std::string s;
    for (auto& t : g_timerStats) s += StrFormat("%-18s %6.2f ms\n", t.name.c_str(), t.ms);
    return s;
}

static bool dbgWriteBmp(const char* path, const u8* pixels, int w, int h, u32 pitch) {
    int rowBytes = w * 3, pad = (4 - rowBytes % 4) % 4;
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    u32 dataSize = (u32)((rowBytes + pad) * h);
    u8 hdr[54] = {'B', 'M'};
    u32 fileSize = 54 + dataSize;
    memcpy(hdr + 2, &fileSize, 4);
    u32 off = 54, hsz = 40, planes = 1, bpp = 24;
    memcpy(hdr + 10, &off, 4);
    memcpy(hdr + 14, &hsz, 4);
    memcpy(hdr + 18, &w, 4);
    memcpy(hdr + 22, &h, 4);
    memcpy(hdr + 26, &planes, 2);
    memcpy(hdr + 28, &bpp, 2);
    memcpy(hdr + 34, &dataSize, 4);
    fwrite(hdr, 1, 54, f);
    std::vector<u8> row((size_t)(rowBytes + pad), 0);
    for (int y = h - 1; y >= 0; y--) {
        const u8* src = pixels + (size_t)y * pitch;
        for (int x = 0; x < w; x++) {
            row[(size_t)x * 3 + 0] = src[x * 4 + 2];
            row[(size_t)x * 3 + 1] = src[x * 4 + 1];
            row[(size_t)x * 3 + 2] = src[x * 4 + 0];
        }
        fwrite(row.data(), 1, row.size(), f);
    }
    fclose(f);
    return true;
}

static u64 g_dbgShotEarly = 0;
bool saveScreenshotBMP(const char* path) {
    ResourceObj* bb = backbuffer();
    if (!bb) return false;
    u32 bbIndex = g.swap->GetCurrentBackBufferIndex();
    Buffer rb;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    if (!readSubresource(bb, 0, rb, fp)) {
        rb.release();
        return false;
    }
    u64 doneAtRead = g.fence[QUEUE_DIRECT]->GetCompletedValue(), signaled = g.fenceValue[QUEUE_DIRECT];
    const u8* pixels = (const u8*)mapReadback(rb);
    int w = (int)fp.Footprint.Width, h = (int)fp.Footprint.Height;
    u32 pitch = fp.Footprint.RowPitch;
    std::vector<u8> first((size_t)pitch * h);
    memcpy(first.data(), pixels, first.size());
    bool ok = dbgWriteBmp(path, first.data(), w, h, pitch);
    // DBG: wait for the GPU by polling the fence values (not the event), then compare the readback buffer with what
    // the screenshot copied out of it
    for (int q = 0; q < QUEUE_COUNT; q++)
        while (g.fence[q]->GetCompletedValue() < g.fenceValue[q]) Sleep(1);
    size_t diff = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const u8* a = first.data() + (size_t)y * pitch + x * 4;
            const u8* b = pixels + (size_t)y * pitch + x * 4;
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) diff++;
        }
    if (diff) dbgWriteBmp((std::string(path) + ".settled.bmp").c_str(), pixels, w, h, pitch);
    std::string bufs;
    for (u32 i = 0; i < DeviceState::kBackbuffers; i++) {
        ID3D12Resource* cur = nullptr;
        if (SUCCEEDED(g.swap->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&cur)) && cur) {
            bufs += cur == g.bb[i]->d3d ? "same " : "CHANGED ";
            cur->Release();
        } else bufs += "GetBuffer-failed ";
    }
    LOG("DBG screenshot %s: frame %llu, back buffer %u, direct fence completed %llu of %llu at the read; first read differs from the settled "
        "copy in %.2f%% of pixels; early waits %llu (+%llu since the last shot), races %llu, waits %llu; swap chain buffers %s",
        path, (unsigned long long)g.frame, bbIndex, (unsigned long long)doneAtRead, (unsigned long long)signaled, 100.0 * diff / ((double)w * h),
        (unsigned long long)g_dbgEarly, (unsigned long long)(g_dbgEarly - g_dbgShotEarly), (unsigned long long)g_dbgRaces,
        (unsigned long long)g_dbgWaits, bufs.c_str());
    g_dbgShotEarly = g_dbgEarly;
    rb.release();
    return ok;
}

bool readbackPixelsFloat4(Resource tex, DXGI_FORMAT fmt, int x, int y, float out[4]) {
    if (!tex || tex->kind == RES_BUFFER) return false;
    checkAlive(tex, "readbackPixelsFloat4");
    Buffer rb;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
    if (!readSubresource(tex, 0, rb, fp)) {
        rb.release();
        return false;
    }
    x = Clamp(x, 0, (int)fp.Footprint.Width - 1);
    y = Clamp(y, 0, (int)fp.Footprint.Height - 1);
    const u8* row = (const u8*)mapReadback(rb) + (size_t)y * fp.Footprint.RowPitch;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (fmt == DXGI_FORMAT_R16G16B16A16_FLOAT) {
        const u16* p = (const u16*)(row + (size_t)x * 8);
        for (int i = 0; i < 4; i++) out[i] = halfToFloat(p[i]);
    } else if (fmt == DXGI_FORMAT_R32_FLOAT || fmt == DXGI_FORMAT_R32_TYPELESS) {
        memcpy(out, row + (size_t)x * 4, 4);
    } else {
        const u8* p = row + (size_t)x * 4;
        for (int i = 0; i < 4; i++) out[i] = p[i] / 255.f;
    }
    rb.release();
    return true;
}

bool readbackBuffer(Resource buf, void* out, u32 size) {
    if (!buf || buf->kind != RES_BUFFER) return false;
    checkAlive(buf, "readbackBuffer");
    if (buf->upload) {
        if (!buf->uploadCpu) return false;
        memcpy(out, buf->uploadCpu, Min(size, buf->uploadSize));
        return true;
    }
    u32 n = Min(size, buf->size);
    Buffer rb = createBuffer(n, 0, BUF_READBACK, nullptr, "readback");
    ctx->copyBufferRegion(rb.buf, 0, buf, 0, n);
    waitIdle();
    const void* p = mapReadback(rb);
    if (p) memcpy(out, p, n);
    rb.release();
    return p != nullptr;
}

// ---------------------------------------------------------------------------------------------------------------
// Self-test
static bool runningOnWine() {
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    return nt && GetProcAddress(nt, "wine_get_version") != nullptr;
}

// Self-test 14: signals the shared fence event every millisecond while a wait is in progress, the way the signals of
// earlier waits can arrive late
static std::atomic<bool> g_straySignals{false};
static DWORD WINAPI straySignalThread(void*) {
    while (g_straySignals.load()) {
        SetEvent(g.fenceEvent);
        Sleep(1);
    }
    return 0;
}

// h -> h * 1664525 + 1013904223 (mod 2^32) applied n times, as one map h -> a * h + c (the csSpin results)
static void lcgSteps(u32 n, u32& a, u32& c) {
    u32 ma = 1664525u, mc = 1013904223u;   // the map applied 2^k times
    a = 1;
    c = 0;
    for (; n; n >>= 1) {
        if (n & 1) {
            a *= ma;
            c = c * ma + mc;
        }
        mc = mc * ma + mc;
        ma *= ma;
    }
}

int selfTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* name, const std::string& detail) {
        LOG("gfx self-test %-34s %s  %s", name, ok ? "PASS" : "FAIL", detail.c_str());
        if (!ok) failures++;
    };
    LOG("gfx self-test on %s", featureSummary().c_str());
    const bool tier3 = g.bindingTier >= D3D12_RESOURCE_BINDING_TIER_3;
    ComputeShader csBindless = loadCS("gfxtest.hlsl", "csBindless");
    ComputeShader csWriteSeq = loadCS("gfxtest.hlsl", "csWriteSeq");
    ComputeShader csDouble = loadCS("gfxtest.hlsl", "csDouble");
    ComputeShader csMakeArgs = loadCS("gfxtest.hlsl", "csMakeArgs");
    ComputeShader csIndirect = loadCS("gfxtest.hlsl", "csIndirectTarget");
    ComputeShader csAppend = loadCS("gfxtest.hlsl", "csAppend");
    VertexShader vsFull = loadVS("gfxtest.hlsl", "vsFull", nullptr, 0);
    PixelShader psTest = loadPS("gfxtest.hlsl", "psTest");
    PixelShader psDepth = loadPS("gfxtest.hlsl", "psDepthRead");
    PixelShader psSolid = loadPS("gfxtest.hlsl", "psSolid");
    const u32 N = 64;

    // 1) bindless arrays + root constants + root CBV + slot tables in one dispatch
    {
        u32 texels[4] = {0xff00000au, 0xff000014u, 0xff00001eu, 0xff000028u};   // red 10, 20, 30, 40
        Texture tex = createTexture2D(4, 1, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_SRV, 1, 1, texels, 16);
        u32 green = 0xff000700u;   // g = 7
        Texture local = createTexture2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_SRV, 1, 1, &green, 4);
        std::vector<u32> seq(N);
        for (u32 i = 0; i < N; i++) seq[i] = i;
        Buffer raw = createBuffer(N * 4, 4, BUF_RAW | BUF_SRV, seq.data(), "selftest raw");
        Buffer out = createBuffer(N * 4, 4, BUF_RAW | BUF_UAV, nullptr, "selftest raw out");
        Buffer outLocal = createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest local out");
        CBuffer<u32[4]> cb;
        cb.create();
        cb.data[0] = 3;
        cb.upload();
        u32 rc[4] = {bindlessIndex(tex.srv), bindlessIndex(raw.srv), tier3 ? bindlessIndex(out.uav) : ~0u, 1000000};
        ctx->setCS(csBindless);
        ctx->setRootConstants(true, 0, 4, rc);
        Resource cbs[2] = {nullptr, cb.get()};
        ctx->csSetCBs(0, 2, cbs);
        ctx->csSetSRVs(3, 1, &local.srv);
        ctx->csSetUAVs(2, 1, &outLocal.uav);
        if (tier3) ctx->transition(out.buf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);   // bindless UAVs are not tracked
        ctx->dispatch(1, 1, 1);
        std::vector<u32> a(N), b(N);
        readbackBuffer(outLocal.buf, a.data(), N * 4);
        if (tier3) readbackBuffer(out.buf, b.data(), N * 4);
        int bad = 0;
        for (u32 i = 0; i < N; i++) {
            u32 expect = 10 * (i % 4 + 1) + i * 1000 + 3 + 1000000 + 7;
            if (a[i] != expect || (tier3 && b[i] != expect)) bad++;
        }
        check(bad == 0, "bindless + root constants + tables", StrFormat("[5] = %u (expect %u)%s", a[5], 20 + 5000 + 1000010, tier3 ? "" : ", no bindless UAVs (tier 2)"));
        unbindCSResources(8, 8);
        tex.release();
        local.release();
        raw.release();
        out.release();
        outLocal.release();
        cb.release();
    }

    // 2) graphics: PS table + root CBV + root constants into a render target
    Texture rt = createTexture2D(16, 16, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_RTV | TEX_SRV);
    {
        u32 r200 = 0xff0000c8u;
        Texture src = createTexture2D(1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_SRV, 1, 1, &r200, 4);
        CBuffer<u32[4]> cb;
        cb.create();
        cb.data[0] = 100;
        cb.upload();
        u32 rc[4] = {50, 0, 0, 0};
        ctx->setRenderTargets(1, &rt.rtv, nullptr);
        setViewport(16, 16);
        ctx->setVS(vsFull.vs);
        ctx->setInputLayout(nullptr);
        ctx->setPS(psTest);
        ctx->setBlendState(states.opaque);
        ctx->setDepthState(states.depthOff);
        ctx->setRasterState(states.cullNone);
        ctx->setTopology(TOPO_TRIANGLE_LIST);
        ctx->psSetSRVs(0, 1, &src.srv);
        Resource cbs[2] = {nullptr, cb.get()};
        ctx->psSetCBs(0, 2, cbs);
        ctx->setRootConstants(false, 0, 4, rc);
        ctx->draw(3, 0);
        ctx->setRenderTargets(0, nullptr, nullptr);
        float px[4];
        readbackPixelsFloat4(rt.res, DXGI_FORMAT_R8G8B8A8_UNORM, 8, 8, px);
        bool ok = fabsf(px[0] * 255.f - 200.f) < 1.5f && fabsf(px[1] * 255.f - 100.f) < 1.5f && fabsf(px[2] * 255.f - 50.f) < 1.5f;
        check(ok, "draw: tables + root CBV + constants", StrFormat("pixel %.0f %.0f %.0f (expect 200 100 50)", px[0] * 255.f, px[1] * 255.f, px[2] * 255.f));
        SRV nul = nullptr;
        ctx->psSetSRVs(0, 1, &nul);
        src.release();
        cb.release();
    }

    // 3) DrawInstancedIndirect (CPU-written arguments) and read-only depth sampled while bound
    {
        D3D12_DRAW_ARGUMENTS da = {3, 1, 0, 0};
        Buffer args = createBuffer(sizeof(da), 4, BUF_INDIRECT, &da, "selftest draw args");
        u32 color[8] = {0, 0, 0, 0, 30, 60, 90, 255};
        ctx->setRootConstants(false, 0, 8, color);
        ctx->setRenderTargets(1, &rt.rtv, nullptr);
        ctx->setPS(psSolid);
        ctx->drawInstancedIndirect(args.buf, 0);
        ctx->setRenderTargets(0, nullptr, nullptr);
        float px[4];
        readbackPixelsFloat4(rt.res, DXGI_FORMAT_R8G8B8A8_UNORM, 3, 12, px);
        bool ok = fabsf(px[0] * 255.f - 30.f) < 1.5f && fabsf(px[1] * 255.f - 60.f) < 1.5f && fabsf(px[2] * 255.f - 90.f) < 1.5f;
        check(ok, "DrawInstancedIndirect", StrFormat("pixel %.0f %.0f %.0f (expect 30 60 90)", px[0] * 255.f, px[1] * 255.f, px[2] * 255.f));
        args.release();

        Texture depth = createTexture2D(16, 16, DXGI_FORMAT_R32_TYPELESS, TEX_DSV | TEX_SRV | TEX_DSV_READONLY);
        Texture depthCopy = createTexture2D(16, 16, DXGI_FORMAT_R32_TYPELESS, TEX_SRV);
        Texture fout = createTexture2D(16, 16, DXGI_FORMAT_R32_FLOAT, TEX_RTV | TEX_SRV);
        ctx->clearDepth(depth.dsv, 0.25f);
        ctx->copyResource(depthCopy.res, depth.res);
        float cp[4];
        readbackPixelsFloat4(depthCopy.res, DXGI_FORMAT_R32_FLOAT, 5, 5, cp);
        check(fabsf(cp[0] - 0.25f) < 1e-6f, "copy depth -> R32 texture", StrFormat("%.4f (expect 0.25)", cp[0]));
        ctx->setRenderTargets(1, &fout.rtv, depth.dsvRO);
        ctx->setDepthState(states.depthGreaterEqualNoWrite);
        ctx->setPS(psDepth);
        ctx->psSetSRVs(0, 1, &depth.srv);
        ctx->draw(3, 0);
        ctx->setRenderTargets(0, nullptr, nullptr);
        SRV nul = nullptr;
        ctx->psSetSRVs(0, 1, &nul);
        float dv[4];
        readbackPixelsFloat4(fout.res, DXGI_FORMAT_R32_FLOAT, 7, 9, dv);
        check(fabsf(dv[0] - 0.25f) < 1e-6f, "read-only depth + depth SRV", StrFormat("%.4f (expect 0.25)", dv[0]));
        ctx->setDepthState(states.depthOff);
        depth.release();
        depthCopy.release();
        fout.release();
    }

    // 4) async compute: direct writes a sequence, the compute queue doubles it after a cross-queue wait,
    //    the direct queue waits for the compute queue and reads the result
    {
        Buffer a = createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest seq");
        Buffer b = createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest doubled");
        u32 rc = 5;
        ctx->setCS(csWriteSeq);
        ctx->setRootConstants(true, 0, 1, &rc);
        ctx->csSetUAVs(0, 1, &a.uav);
        ctx->dispatch(1, 1, 1);
        unbindCSResources(1, 1);
        ctx->transition(a.buf, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        u64 directDone = ctx->submit();
        Context* ac = beginAsyncCompute();
        ac->wait(QUEUE_DIRECT, directDone);
        ac->setCS(csDouble);
        ac->csSetSRVs(0, 1, &a.srv);
        ac->csSetUAVs(0, 1, &b.uav);
        ac->dispatch(1, 1, 1);
        u64 computeDone = submitAsyncCompute(ac);
        ctx->wait(QUEUE_COMPUTE, computeDone);
        std::vector<u32> r(N);
        readbackBuffer(b.buf, r.data(), N * 4);
        int bad = 0;
        for (u32 i = 0; i < N; i++) bad += r[i] != (i + 5) * 2;
        check(bad == 0, "async compute + cross-queue fences", StrFormat("[3] = %u (expect 16), compute fence %llu", r[3], (unsigned long long)computeDone));
        a.release();
        b.release();
    }

    // 5) ExecuteIndirect: GPU-written dispatch arguments; multi-draw with a GPU count buffer; per-command root
    //    constants (vkd3d 1.10 implements neither root-constant arguments nor count buffers for dispatches, so that
    //    last case is skipped under Wine)
    {
        Buffer args = createBuffer(3 * 16, 4, BUF_RAW | BUF_UAV, nullptr, "selftest indirect args");
        Buffer count = createBuffer(4, 4, BUF_RAW | BUF_UAV, nullptr, "selftest indirect count");
        Buffer target = createBuffer(4 * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest indirect target");
        u32 zero[4] = {0, 0, 0, 0};
        ctx->clearUAVUint(target.uav, zero);
        ctx->setCS(csMakeArgs);
        UAV u[2] = {args.uav, count.uav};
        ctx->csSetUAVs(0, 2, u);
        ctx->dispatch(1, 1, 1);
        unbindCSResources(2, 2);
        // command 1 of the generated list: dispatch (1,1,1) with the root constant set on the CPU
        u32 rc = 2;
        ctx->setCS(csIndirect);
        ctx->setRootConstants(true, 0, 1, &rc);
        ctx->csSetUAVs(0, 1, &target.uav);
        ctx->dispatchIndirect(args.buf, 16 + 4);
        unbindCSResources(1, 1);
        u32 r[4] = {};
        readbackBuffer(target.buf, r, 16);
        check(r[2] == 102 && r[0] == 0, "ExecuteIndirect: GPU-written dispatch", StrFormat("%u (expect 102)", r[2]));
        if (!runningOnWine()) {
            CommandSignature sig = createCommandSignature(INDIRECT_DISPATCH, 1);
            ctx->clearUAVUint(target.uav, zero);
            ctx->setCS(csIndirect);
            ctx->csSetUAVs(0, 1, &target.uav);
            ctx->executeIndirect(sig, 3, args.buf, 0, count.buf, 0);
            unbindCSResources(1, 1);
            readbackBuffer(target.buf, r, 16);
            check(r[0] == 100 && r[1] == 101 && r[2] == 0, "ExecuteIndirect: constants + count", StrFormat("%u %u %u (expect 100 101 0)", r[0], r[1], r[2]));
        } else LOG("gfx self-test %-34s SKIP  vkd3d has no root-constant arguments or dispatch count buffers", "ExecuteIndirect: constants + count");
        // multi-draw indirect with a count buffer: three additive fullscreen draws requested, the count allows two
        D3D12_DRAW_ARGUMENTS da[3] = {{3, 1, 0, 0}, {3, 1, 0, 0}, {3, 1, 0, 0}};
        Buffer drawArgs = createBuffer(sizeof(da), 4, BUF_INDIRECT, da, "selftest multi-draw args");
        u32 two = 2;
        Buffer drawCount = createBuffer(4, 4, BUF_INDIRECT, &two, "selftest multi-draw count");
        CommandSignature drawSig = createCommandSignature(INDIRECT_DRAW);
        float black[4] = {0, 0, 0, 0};
        ctx->clearRTV(rt.rtv, black);
        u32 color[8] = {0, 0, 0, 0, 40, 20, 10, 0};
        ctx->setRootConstants(false, 0, 8, color);
        ctx->setRenderTargets(1, &rt.rtv, nullptr);
        setViewport(16, 16);
        ctx->setVS(vsFull.vs);
        ctx->setPS(psSolid);
        ctx->setBlendState(states.additive);
        ctx->setDepthState(states.depthOff);
        ctx->executeIndirect(drawSig, 3, drawArgs.buf, 0, drawCount.buf, 0);
        ctx->setRenderTargets(0, nullptr, nullptr);
        ctx->setBlendState(states.opaque);
        float px[4];
        readbackPixelsFloat4(rt.res, DXGI_FORMAT_R8G8B8A8_UNORM, 6, 6, px);
        check(fabsf(px[0] * 255.f - 80.f) < 1.5f && fabsf(px[1] * 255.f - 40.f) < 1.5f, "ExecuteIndirect: multi-draw + count",
              StrFormat("pixel %.0f %.0f %.0f (expect 80 40 20)", px[0] * 255.f, px[1] * 255.f, px[2] * 255.f));
        args.release();
        count.release();
        target.release();
        drawArgs.release();
        drawCount.release();
    }

    // 6) append counter: reset through initialCounts, count copied out (CopyStructureCount)
    {
        Buffer app = createBuffer(256 * 4, 4, BUF_STRUCTURED | BUF_UAV | BUF_APPEND, nullptr, "selftest append");
        Buffer cnt = createBuffer(16, 4, BUF_INDIRECT, nullptr, "selftest count");
        u32 n = 37, zeroCount = 0;
        for (int pass = 0; pass < 2; pass++) {
            ctx->setCS(csAppend);
            ctx->setRootConstants(true, 0, 1, &n);
            ctx->csSetUAVs(0, 1, &app.uav, &zeroCount);
            ctx->dispatch(4, 1, 1);
            unbindCSResources(1, 1);
        }
        ctx->copyStructureCount(cnt.buf, 4, app.uav);
        u32 r[4] = {};
        readbackBuffer(cnt.buf, r, 16);
        check(r[1] == 37, "append counter + CopyStructureCount", StrFormat("%u (expect 37: the counter restarts at 0 each pass)", r[1]));
        app.release();
        cnt.release();
    }

    // 7) placed resources sharing memory: aliasing barriers, clears initialize the newly active one
    {
        TextureDesc d;
        d.width = 32;
        d.height = 32;
        d.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.flags = TEX_RTV | TEX_SRV;
        AllocInfo ai = textureAllocInfo(d);
        Heap heap = createHeap(ai.size, HEAP_RT_DS_TEXTURES);
        Texture ta = createPlacedTexture(heap, 0, d);
        Texture tb = createPlacedTexture(heap, 0, d);
        float red[4] = {1, 0, 0, 1}, green[4] = {0, 1, 0, 1};
        ctx->aliasingBarrier(nullptr, ta.res);
        ctx->discard(ta.res);
        ctx->clearRTV(ta.rtv, red);
        float pa[4], pb[4];
        readbackPixelsFloat4(ta.res, DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, pa);
        ctx->aliasingBarrier(ta.res, tb.res);
        ctx->discard(tb.res);
        ctx->clearRTV(tb.rtv, green);
        readbackPixelsFloat4(tb.res, DXGI_FORMAT_R8G8B8A8_UNORM, 4, 4, pb);
        check(pa[0] > 0.99f && pa[1] < 0.01f && pb[1] > 0.99f && pb[0] < 0.01f, "placed resources + aliasing",
              StrFormat("A %.0f %.0f, B %.0f %.0f (heap %llu KB)", pa[0] * 255.f, pa[1] * 255.f, pb[0] * 255.f, pb[1] * 255.f,
                        (unsigned long long)(ai.size >> 10)));
        ta.release();
        tb.release();
        releaseHeap(heap);
    }

    // 8) mip generation (sRGB re-encoding, per-mip transitions)
    {
        const int S = 8;
        std::vector<u32> px((size_t)S * S);
        for (int y = 0; y < S; y++)
            for (int x = 0; x < S; x++) px[(size_t)y * S + x] = ((x + y) & 1) ? 0xffffffffu : 0xff000000u;   // checkerboard
        TextureDesc d;
        d.width = d.height = S;
        d.mips = 0;
        d.format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        d.flags = TEX_SRV | TEX_GENMIPS;
        d.name = "selftest mips";
        Texture t = createTexture(d, px.data(), S * 4);
        ctx->generateMips(t);
        Buffer rb;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp;
        readSubresource(t.res, (u32)t.mips - 1, rb, fp);
        const u8* p = (const u8*)mapReadback(rb);
        // average of black and white in linear light (0.5) re-encoded as sRGB: 188
        check(p && abs((int)p[0] - 188) <= 2 && t.mips == 4, "mip generation (sRGB)", StrFormat("1x1 mip = %d (expect ~188), %d mips", p ? p[0] : -1, t.mips));
        rb.release();
        t.release();
    }

    rt.release();
    // 12) one long command list: a chain of dependent dispatches, each with freshly built SRV and UAV tables
    //     (descriptor copies made while the list records) and a UAV -> SRV transition per link, no submit between
    {
        const u32 kLinks = 24;
        Buffer chain[2] = {createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest chain A"),
                           createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest chain B")};
        u32 rc = 1;
        // a long list before the chain: thousands of dispatches with alternating UAV tables
        const char* stressArg = Platform::argValue("gfxstress");
        u32 stress = stressArg ? (u32)atoi(stressArg) : 0;
        Buffer dummy[2] = {createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest dummy A"),
                           createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest dummy B")};
        ctx->setCS(csWriteSeq);
        for (u32 k = 0; k < stress; k++) {
            ctx->setRootConstants(true, 0, 1, &k);
            ctx->csSetUAVs(0, 1, &dummy[k % 2].uav);
            ctx->dispatch(1, 1, 1);
        }
        ctx->setRootConstants(true, 0, 1, &rc);
        ctx->csSetUAVs(0, 1, &chain[0].uav);
        ctx->dispatch(1, 1, 1);
        ctx->setCS(csDouble);
        for (u32 k = 0; k < kLinks; k++) {
            UAV nu = nullptr;
            ctx->csSetUAVs(0, 1, &nu);
            ctx->csSetSRVs(0, 1, &chain[k % 2].srv);
            ctx->csSetUAVs(0, 1, &chain[(k + 1) % 2].uav);
            ctx->dispatch(1, 1, 1);
            SRV ns = nullptr;
            ctx->csSetSRVs(0, 1, &ns);
        }
        unbindCSResources(1, 1);
        std::vector<u32> r(N);
        readbackBuffer(chain[kLinks % 2].buf, r.data(), N * 4);
        int bad = 0;
        for (u32 i = 0; i < N; i++) bad += r[i] != ((i + 1u) << kLinks);
        check(bad == 0, "dependent dispatch chain, one list", StrFormat("[3] = %u (expect %u), %d wrong", r[3], 4u << kLinks, bad));
        chain[0].release();
        chain[1].release();
        dummy[0].release();
        dummy[1].release();
    }

    // 13) root arguments survive a UAV clear: the global SRV table (t40) and the root constants set before the clear
    //     are still in effect for the dispatch after it
    {
        ComputeShader csReadGlobal = loadCS("gfxtest.hlsl", "csReadGlobal");
        std::vector<u32> seq(N);
        for (u32 i = 0; i < N; i++) seq[i] = i * 3;
        Buffer src = createBuffer(N * 4, 4, BUF_STRUCTURED, seq.data(), "selftest global src");
        Buffer outA = createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest global out A");
        Buffer outB = createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest global out B");
        Texture clr = createTexture2D(16, 16, DXGI_FORMAT_R16G16B16A16_FLOAT, TEX_SRV | TEX_UAV);
        u32 rc = 7;
        ctx->setCS(csReadGlobal);
        ctx->setRootConstants(true, 0, 1, &rc);
        ctx->csSetSRVs(40, 1, &src.srv);
        ctx->csSetUAVs(0, 1, &outA.uav);
        ctx->dispatch(1, 1, 1);
        const float clearValue[4] = {0.f, 0.f, 0.f, 1.f};
        ctx->clearUAVFloat(clr.uav, clearValue);
        ctx->csSetUAVs(0, 1, &outB.uav);
        ctx->dispatch(1, 1, 1);
        unbindCSResources(1, 1);
        SRV ns = nullptr;
        ctx->csSetSRVs(40, 1, &ns);
        std::vector<u32> r(N);
        readbackBuffer(outB.buf, r.data(), N * 4);
        int bad = 0;
        for (u32 i = 0; i < N; i++) bad += r[i] != i * 3 + 7;
        check(bad == 0, "root arguments across a UAV clear", StrFormat("[5] = %u (expect 22), %d wrong", r[5], bad));
        src.release();
        outA.release();
        outB.release();
        clr.release();
        releaseShader(csReadGlobal);
    }

    // 14) 15) CPU waits end when the fence reaches its value, whatever happens to the shared wait event: a readback
    //     right after long GPU work (screenshots, photo mode) gets the finished results. 14: a second thread signals
    //     the event every millisecond during the wait. 15: waits that poll every millisecond, so they time out and
    //     race the completion of their fence again and again.
    {
        ComputeShader csSpin = loadCS("gfxtest.hlsl", "csSpin");
        Buffer out = createBuffer(N * 4, 4, BUF_STRUCTURED | BUF_UAV, nullptr, "selftest spin out");
        auto spinRound = [&](u32 steps, u32 seed) {
            u32 rc[2] = {steps, seed};
            ctx->setCS(csSpin);
            ctx->setRootConstants(true, 0, 2, rc);
            ctx->csSetUAVs(0, 1, &out.uav);
            ctx->dispatch(1, 1, 1);
            unbindCSResources(1, 1);
            std::vector<u32> r(N, 0);
            readbackBuffer(out.buf, r.data(), N * 4);
            u32 a, c;
            lcgSteps(steps, a, c);
            int bad = 0;
            for (u32 i = 0; i < N; i++) bad += r[i] != a * (i ^ seed) + c;
            return bad;
        };
        const u32 kSteps = 2000000;   // tens of milliseconds on a software rasterizer, a few on a GPU
        u32 poll = g.fenceWaitPollMs;
        g.fenceWaitPollMs = 1;
        int bad15 = 0;
        for (u32 k = 0; k < 24; k++) bad15 += spinRound(kSteps / 8, 100 + k);
        g.fenceWaitPollMs = poll;
        check(bad15 == 0, "GPU waits polling every millisecond", StrFormat("%d of %u values wrong in 24 readbacks", bad15, N * 24));
        g_straySignals = true;
        HANDLE th = CreateThread(nullptr, 0, straySignalThread, nullptr, 0, nullptr);
        int bad14 = spinRound(kSteps, 0x5a5a);
        g_straySignals = false;
        if (th) {
            WaitForSingleObject(th, INFINITE);
            CloseHandle(th);
        }
        check(th && bad14 == 0, "GPU wait with stray event signals", StrFormat("%d of %u values wrong", bad14, N));
        out.release();
        releaseShader(csSpin);
    }

    releaseShader(csBindless);
    releaseShader(csWriteSeq);
    releaseShader(csDouble);
    releaseShader(csMakeArgs);
    releaseShader(csIndirect);
    releaseShader(csAppend);
    releaseShader(vsFull.vs);
    releaseShader(psTest);
    releaseShader(psDepth);
    releaseShader(psSolid);
    waitIdle();
    LOG("gfx self-test: %d failure(s), %d pipelines, D3D12 debug-layer errors %d", failures, g.psoCount, d3d12ErrorCount());
    return failures;
}

}  // namespace gfx
