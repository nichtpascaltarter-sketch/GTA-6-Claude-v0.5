// Direct3D 12 graphics layer: device, queues, descriptor heaps, upload pages, frames, resources and views.
// The shader system, the command context and the tools (mips, readbacks, timers, self-test) are in the files
// included at the end (unity build).
#include "gfx.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <d3d12sdklayers.h>
#include <dxgi1_5.h>
#include <deque>
#include <unordered_map>
#include "../core/rng.h"
#include "../platform/platform.h"
#include "shaders_embedded.h"

namespace gfx {

Context* ctx = nullptr;
States states;

namespace {

// ---------------------------------------------------------------------------------------------------------------
constexpr u32 kGpuHeapSize = 1000000;            // shader-visible CBV/SRV/UAV heap (the tier 1/2 maximum)
constexpr u32 kPersistentDescriptors = 262144;   // [0, this): bindless region; the rest is the per-frame ring
constexpr u32 kCpuBlock = 4096;                  // non-shader-visible heaps grow in blocks
constexpr u64 kUploadPageSize = 16ull << 20;
constexpr u64 kDedicatedThreshold = 8ull << 20;
constexpr u64 kStagingSubmitBytes = 256ull << 20;
constexpr u32 kSamplerHeapSize = 16;
constexpr size_t kQuarantine = 8192;             // released views / resource records kept to catch later use
// Released GPU objects and retired upload pages wait for the fences of the work that may use them and, on top of
// that, for this many presented frames (the frames in flight plus one).
constexpr u64 kReleaseFrames = kFramesInFlight + 1;

// d3d12.dll is loaded at run time rather than imported: where it does not exist (Windows 7 / 8.1) the game can then
// explain the requirement in a message box instead of failing to start with a loader error.
typedef HRESULT(WINAPI* PFN_SerializeRootSignature)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
PFN_D3D12_CREATE_DEVICE pD3D12CreateDevice = nullptr;
PFN_D3D12_GET_DEBUG_INTERFACE pD3D12GetDebugInterface = nullptr;
PFN_SerializeRootSignature pD3D12SerializeRootSignature = nullptr;

// Wine's builtin DXGI rebuilds its Vulkan swap chain in its present thread when the sync interval changes, while
// earlier presents may still be in flight; on Wine 9.0 with lavapipe that corrupts device memory (descriptors get
// overwritten with presented pixels, then a crash). Its FIFO mode (sync interval 1) can also hold the queue for a
// long time under Xvfb. With that DXGI every Present uses sync interval 0: the one rebuild happens at the first
// Present, before anything is in flight. Builtin Wine modules carry "Wine builtin DLL" at offset 0x40 of their image.
bool wineBuiltinDxgi() {
    HMODULE m = GetModuleHandleA("dxgi.dll");
    return m && memcmp((const char*)m + 0x40, "Wine builtin DLL", 16) == 0;
}

bool loadD3D12() {
    if (pD3D12CreateDevice) return true;
    HMODULE m = LoadLibraryA("d3d12.dll");
    if (!m) return false;
    pD3D12CreateDevice = (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(m, "D3D12CreateDevice");
    pD3D12GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)(void*)GetProcAddress(m, "D3D12GetDebugInterface");
    pD3D12SerializeRootSignature = (PFN_SerializeRootSignature)(void*)GetProcAddress(m, "D3D12SerializeRootSignature");
    return pD3D12CreateDevice && pD3D12SerializeRootSignature;
}

struct CpuDescriptorAllocator {
    D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    u32 inc = 0;
    std::vector<ID3D12DescriptorHeap*> blocks;
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> starts;
    std::vector<u32> freeSlots;
    u32 next = 0;
    u32 alloc(D3D12_CPU_DESCRIPTOR_HANDLE& out);
    void free(u32 slot) { if (slot != ~0u) freeSlots.push_back(slot); }
    D3D12_CPU_DESCRIPTOR_HANDLE handle(u32 slot) const {
        D3D12_CPU_DESCRIPTOR_HANDLE h = starts[slot / kCpuBlock];
        h.ptr += (SIZE_T)(slot % kCpuBlock) * inc;
        return h;
    }
    void release() {
        for (auto* b : blocks) b->Release();
        blocks.clear();
        starts.clear();
    }
};

struct UploadPage {
    ID3D12Resource* res = nullptr;
    u8* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
    u64 size = 0, used = 0;
    u64 retire[QUEUE_COUNT] = {};   // fence values after which no GPU work reads the page any more
    u64 minFrame = 0;               // and not before this frame (see kReleaseFrames)
};

struct UploadAlloc {
    ID3D12Resource* res = nullptr;
    u64 offset = 0;
    u8* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
};

struct Deferred {
    IUnknown* obj = nullptr;
    u32 bindlessSlot = ~0u;          // persistent bindless descriptor slot to recycle
    ResourceObj* resource = nullptr; // resource bookkeeping to delete
    u64 fence[QUEUE_COUNT] = {};
    u64 minFrame = 0;                // not before this frame (see kReleaseFrames)
};

struct FrameData {
    u64 number = 0;
    u64 fence[QUEUE_COUNT] = {};
    std::vector<UploadPage*> pages;          // dynamic upload pages (contents referenced by this frame)
    std::vector<ResourceObj*> written;       // upload buffers written this frame (carried forward if not rewritten)
    u32 ringUsed = 0;                        // descriptor ring usage (including wasted tails)
    // GPU timestamps of the frame
    u32 timerCount = 0;
    std::vector<std::string> timerNames;
    bool timersResolved = false;
};

struct AllocatorEntry {
    ID3D12CommandAllocator* a = nullptr;
    u64 fence = 0;
};

struct PsoKey {
    u32 vs, ps, il, blend, raster, depth;
    u32 topoType, numRT, flags;
    DXGI_FORMAT rt[8];
    DXGI_FORMAT dsv;
    bool operator==(const PsoKey& o) const { return memcmp(this, &o, sizeof(PsoKey)) == 0; }
};
struct PsoKeyHash {
    size_t operator()(const PsoKey& k) const { return (size_t)hash64(&k, sizeof(k)); }
};

struct DeviceState {
    IDXGIFactory4* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    ID3D12Device* dev = nullptr;
    ID3D12InfoQueue* infoQueue = nullptr;
    ID3D12CommandQueue* queue[QUEUE_COUNT] = {};
    ID3D12Fence* fence[QUEUE_COUNT] = {};
    u64 fenceValue[QUEUE_COUNT] = {};        // last value signaled
    HANDLE fenceEvent = nullptr;
    IDXGISwapChain3* swap = nullptr;
    bool allowTearing = false;
    int lastSyncInterval = -1;               // of the previous Present
    bool fixedSyncInterval = false;          // always present with sync interval 0 (see wineBuiltinDxgi)
    int bbW = 0, bbH = 0;
    static const u32 kBackbuffers = 3;
    ResourceObj* bb[kBackbuffers] = {};
    ViewObj* bbRtv[kBackbuffers] = {};
    std::string adapterName;
    size_t vramMB = 0;
    D3D12_RESOURCE_BINDING_TIER bindingTier = D3D12_RESOURCE_BINDING_TIER_1;
    D3D12_RESOURCE_HEAP_TIER heapTier = D3D12_RESOURCE_HEAP_TIER_1;
    bool typedUavLoads = false;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool debugLayer = false;
    int debugErrors = 0;
    std::string initError;
    // descriptor heaps
    ID3D12DescriptorHeap* gpuHeap = nullptr;
    ID3D12DescriptorHeap* samplerHeap = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE samplerHeapGpu = {};
    D3D12_CPU_DESCRIPTOR_HANDLE gpuHeapCpu = {};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHeapGpu = {};
    u32 descInc = 0;
    u32 persistentNext = 0;
    std::vector<u32> persistentFree;
    u32 ringBase = kPersistentDescriptors, ringSize = kGpuHeapSize - kPersistentDescriptors;
    u32 ringHead = 0;        // offset within the ring
    u32 ringUsed = 0;        // allocated and not retired
    u32 ringPendingFree = 0; // ring space of the last retired frame (freed when the next frame retires)
    CpuDescriptorAllocator cpuSrv, cpuRtv, cpuDsv;
    std::unordered_map<u32, D3D12_CPU_DESCRIPTOR_HANDLE> nullSrvs, nullUavs;
    D3D12_CPU_DESCRIPTOR_HANDLE nullRtv = {};
    // command allocators (per queue, recycled by fence)
    std::vector<AllocatorEntry> allocators[QUEUE_COUNT];
    u64 listCounter = 0;
    // upload pages
    std::vector<UploadPage*> freePages;          // standard size
    std::vector<UploadPage*> freeDedicated;
    UploadPage* dynPage = nullptr;               // current dynamic page (belongs to the current frame)
    UploadPage* stagingPage = nullptr;           // current staging page
    std::vector<UploadPage*> stagingOpen;        // staging pages written since the last submission
    std::vector<UploadPage*> pagesInFlight;      // retired staging / frame pages waiting for their fences
    u64 stagingBytesSinceSubmit = 0;
    // frames
    u64 frame = 1;
    FrameData frames[kFramesInFlight];
    std::vector<Deferred> deferred;
    // Released views and resource records are kept (marked dead) for a while, so that code still holding a pointer
    // to one is caught when it binds it instead of reading freed memory (see Context::check*).
    std::deque<ViewObj*> deadViews;
    std::deque<ResourceObj*> deadResources;
    std::vector<ResourceObj*> liveUploads;
    int asyncOpen = 0;
    std::vector<Context*> asyncPool;
    // state tracking
    u64 stateEpoch = 1;
    // root signatures and pipelines
    ID3D12RootSignature* rsGraphics = nullptr;
    ID3D12RootSignature* rsCompute = nullptr;
    std::unordered_map<PsoKey, ID3D12PipelineState*, PsoKeyHash> psoCache;
    int psoCount = 0;
    double psoSeconds = 0;
    ID3D12CommandSignature* drawSig = nullptr;
    ID3D12CommandSignature* drawIndexedSig = nullptr;
    ID3D12CommandSignature* dispatchSig = nullptr;
    std::vector<CommandSignatureObj*> commandSignatures;
    // state objects
    std::vector<BlendStateObj*> blendStates;
    std::vector<RasterStateObj*> rasterStates;
    std::vector<DepthStateObj*> depthStates;
    std::vector<InputLayoutObj*> inputLayouts;
    BlendState defaultBlend = nullptr;     // D3D defaults when nothing is bound
    RasterState defaultRaster = nullptr;
    DepthState defaultDepth = nullptr;
    // misc
    Buffer zeroCB;                                // 64 KB of zeros for unbound constant buffers
    ComputeShader mipGenCS = nullptr;
    DWORD mainThread = 0;
};
DeviceState g;

// Code that keeps a view or buffer pointer after releasing it would read freed memory (and, on the GPU, write into
// memory that now belongs to something else): released objects stay marked dead in a quarantine and are refused here.
std::string describeResource(const ResourceObj* r) {
    if (!r) return "null";
    const char* kind = r->kind == RES_BUFFER ? "buffer" : (r->kind == RES_TEXTURE3D ? "3D texture" : "texture");
    std::string s = r->kind == RES_BUFFER ? StrFormat("%s of %u bytes", kind, r->size)
                                          : StrFormat("%s %llux%ux%u, %u mips, format %d", kind, (unsigned long long)r->desc.Width,
                                                      r->desc.Height, (u32)r->desc.DepthOrArraySize, r->mips, (int)r->format);
    if (!r->name.empty()) s += " '" + r->name + "'";
    return s;
}
[[noreturn]] void usedAfterRelease(const char* what, const std::string& desc) {
    FatalError("Direct3D 12: %s uses a released %s. Some code kept the pointer after releasing it.", what, desc.c_str());
}
inline void checkAlive(const ViewObj* v, const char* what) {
    if (v && v->dead) usedAfterRelease(what, std::string(v->kind == VIEW_SRV ? "SRV" : v->kind == VIEW_UAV ? "UAV" : v->kind == VIEW_RTV ? "RTV" : "DSV") +
                                                 " (" + (v->res && !v->res->dead ? describeResource(v->res) : std::string("its resource is gone too")) + ")");
}
inline void checkAlive(const ResourceObj* r, const char* what) {
    if (r && r->dead) usedAfterRelease(what, describeResource(r));
}


const char* hrName(HRESULT hr) {
    switch (hr) {
        case DXGI_ERROR_DEVICE_REMOVED: return "DXGI_ERROR_DEVICE_REMOVED";
        case DXGI_ERROR_DEVICE_HUNG: return "DXGI_ERROR_DEVICE_HUNG";
        case DXGI_ERROR_DEVICE_RESET: return "DXGI_ERROR_DEVICE_RESET";
        case DXGI_ERROR_DRIVER_INTERNAL_ERROR: return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";
        case DXGI_ERROR_INVALID_CALL: return "DXGI_ERROR_INVALID_CALL";
        case E_OUTOFMEMORY: return "E_OUTOFMEMORY";
        case E_INVALIDARG: return "E_INVALIDARG";
        case E_NOTIMPL: return "E_NOTIMPL";
        case S_OK: return "S_OK";
        default: return "error";
    }
}

void drainDebugMessages() {
    if (!g.infoQueue) return;
    UINT64 n = g.infoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; i++) {
        SIZE_T len = 0;
        if (FAILED(g.infoQueue->GetMessage(i, nullptr, &len)) || len == 0) continue;
        std::vector<u8> buf(len);
        D3D12_MESSAGE* m = (D3D12_MESSAGE*)buf.data();
        if (FAILED(g.infoQueue->GetMessage(i, m, &len))) continue;
        if (m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) g.debugErrors++;
        if (m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING)
            LOG("D3D12 %s: %s", m->Severity <= D3D12_MESSAGE_SEVERITY_ERROR ? "ERROR" : "WARNING", m->pDescription);
    }
    g.infoQueue->ClearStoredMessages();
}

// Logs a clear line and exits when the device is gone (removed, hung, reset, driver error).
[[noreturn]] void deviceLost(const char* where, HRESULT hr) {
    HRESULT reason = g.dev ? g.dev->GetDeviceRemovedReason() : hr;
    drainDebugMessages();
    LOG("D3D12 device lost during %s: %08lx (%s), removed reason %08lx (%s)", where, (unsigned long)hr, hrName(hr),
        (unsigned long)reason, hrName(reason));
    FatalError("The graphics device was lost (%s, reason %08lx). Please update your GPU driver and restart the game.",
               hrName(reason), (unsigned long)reason);
}

void checkHR(HRESULT hr, const char* what) {
    if (SUCCEEDED(hr)) return;
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG || hr == DXGI_ERROR_DEVICE_RESET ||
        hr == DXGI_ERROR_DRIVER_INTERNAL_ERROR)
        deviceLost(what, hr);
    drainDebugMessages();
    FatalError("Direct3D 12: %s failed (%08lx %s)", what, (unsigned long)hr, hrName(hr));
}

u32 CpuDescriptorAllocator::alloc(D3D12_CPU_DESCRIPTOR_HANDLE& out) {
    u32 slot;
    if (!freeSlots.empty()) {
        slot = freeSlots.back();
        freeSlots.pop_back();
    } else {
        if (next / kCpuBlock >= blocks.size()) {
            D3D12_DESCRIPTOR_HEAP_DESC hd = {type, kCpuBlock, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
            ID3D12DescriptorHeap* h = nullptr;
            checkHR(g.dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&h), "CreateDescriptorHeap (CPU)");
            blocks.push_back(h);
            starts.push_back(h->GetCPUDescriptorHandleForHeapStart());
        }
        slot = next++;
    }
    out = handle(slot);
    return slot;
}

inline D3D12_CPU_DESCRIPTOR_HANDLE gpuHeapCpu(u32 index) {
    D3D12_CPU_DESCRIPTOR_HANDLE h = g.gpuHeapCpu;
    h.ptr += (SIZE_T)index * g.descInc;
    return h;
}
inline D3D12_GPU_DESCRIPTOR_HANDLE gpuHeapGpu(u32 index) {
    D3D12_GPU_DESCRIPTOR_HANDLE h = g.gpuHeapGpu;
    h.ptr += (UINT64)index * g.descInc;
    return h;
}

inline FrameData& curFrame() { return g.frames[g.frame % kFramesInFlight]; }

// Fence values an object touched now must wait for before it can be destroyed or reused.
void retireTag(u64 out[QUEUE_COUNT]) {
    out[QUEUE_DIRECT] = g.fenceValue[QUEUE_DIRECT] + 1;
    out[QUEUE_COMPUTE] = g.fenceValue[QUEUE_COMPUTE] + (g.asyncOpen > 0 ? 1 : 0);
}
bool tagReached(const u64 tag[QUEUE_COUNT]) {
    for (int q = 0; q < QUEUE_COUNT; q++)
        if (tag[q] > 0 && g.fence[q]->GetCompletedValue() < tag[q]) return false;
    return true;
}

u32 allocPersistent() {
    if (!g.persistentFree.empty()) {
        u32 s = g.persistentFree.back();
        g.persistentFree.pop_back();
        return s;
    }
    if (g.persistentNext >= kPersistentDescriptors) FatalError("Direct3D 12: out of bindless descriptors (%u)", kPersistentDescriptors);
    return g.persistentNext++;
}
void buryResource(ResourceObj* r);

void deferPersistentFree(u32 slot) {
    if (slot == ~0u) return;
    Deferred d;
    d.bindlessSlot = slot;
    retireTag(d.fence);
    d.minFrame = g.frame + kReleaseFrames;
    g.deferred.push_back(d);
}
void deferRelease(IUnknown* obj) {
    if (!obj) return;
    Deferred d;
    d.obj = obj;
    retireTag(d.fence);
    d.minFrame = g.frame + kReleaseFrames;
    g.deferred.push_back(d);
}

void waitForFenceValue(QueueKind q, u64 v);
void retireFrame(FrameData& f);

// Shader-visible ring for per-draw descriptor tables and the bindless slots of upload buffers.
u32 allocRing(u32 n) {
    for (int attempt = 0; attempt < 3; attempt++) {
        FrameData& cur = curFrame();
        u32 waste = (g.ringHead + n > g.ringSize) ? g.ringSize - g.ringHead : 0;
        if (g.ringUsed + waste + n <= g.ringSize) {
            if (waste) {
                g.ringHead = 0;
                g.ringUsed += waste;
                cur.ringUsed += waste;
            }
            u32 idx = g.ringBase + g.ringHead;
            g.ringHead += n;
            if (g.ringHead == g.ringSize) g.ringHead = 0;
            g.ringUsed += n;
            cur.ringUsed += n;
            return idx;
        }
        if (attempt == 0) {
            // retire the previous frame early (wait for its GPU work)
            FrameData& prev = g.frames[(g.frame + kFramesInFlight - 1) % kFramesInFlight];
            if (prev.number && prev.number != g.frame) {
                for (int q = 0; q < QUEUE_COUNT; q++) waitForFenceValue((QueueKind)q, prev.fence[q]);
                retireFrame(prev);
            }
        } else {
            // this frame alone filled the ring: run everything recorded so far and start over
            LOG("Direct3D 12: descriptor ring full within one frame (%u used), flushing", g.ringUsed);
            waitIdle();
            g.ringUsed = 0;
            g.ringHead = 0;
            g.ringPendingFree = 0;
            for (FrameData& fr : g.frames) fr.ringUsed = 0;
            // upload buffers keep their contents: their SRVs get fresh bindless slots in the emptied ring
            for (FrameData& fr : g.frames)
                for (ResourceObj* r : fr.written)
                    for (ViewObj* v : r->views)
                        if (v->kind == VIEW_SRV) {
                            u32 slot = g.ringBase + g.ringHead;
                            g.ringHead++;
                            g.ringUsed++;
                            cur.ringUsed++;
                            g.dev->CopyDescriptorsSimple(1, gpuHeapCpu(slot), v->cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                            v->bindless = slot;
                        }
            if (ctx) ctx->markSRVTablesDirty();
            for (Context* c : g.asyncPool) c->markSRVTablesDirty();
        }
    }
    FatalError("Direct3D 12: descriptor ring allocation of %u failed", n);
}

// ---------------------------------------------------------------------------------------------------------------
// Upload pages
UploadPage* newPage(u64 size) {
    UploadPage* p = new UploadPage();
    p->size = size;
    D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    checkHR(g.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                           __uuidof(ID3D12Resource), (void**)&p->res), "CreateCommittedResource (upload page)");
    D3D12_RANGE none = {0, 0};
    checkHR(p->res->Map(0, &none, (void**)&p->cpu), "Map (upload page)");
    p->gpu = p->res->GetGPUVirtualAddress();
    return p;
}

void destroyPage(UploadPage* p) {
    p->res->Unmap(0, nullptr);
    p->res->Release();
    delete p;
}

UploadPage* getPage(u64 need) {
    if (need <= kUploadPageSize) {
        if (!g.freePages.empty()) {
            UploadPage* p = g.freePages.back();
            g.freePages.pop_back();
            p->used = 0;
            return p;
        }
        return newPage(kUploadPageSize);
    }
    u64 size = 1ull << 20;
    while (size < need) size <<= 1;
    for (size_t i = 0; i < g.freeDedicated.size(); i++)
        if (g.freeDedicated[i]->size == size) {
            UploadPage* p = g.freeDedicated[i];
            g.freeDedicated.erase(g.freeDedicated.begin() + (long)i);
            p->used = 0;
            return p;
        }
    return newPage(size);
}

void recyclePage(UploadPage* p) {
    p->used = 0;
    if (p->size == kUploadPageSize) {
        // keep up to 256 MB of standard pages
        if (g.freePages.size() < 16) g.freePages.push_back(p);
        else destroyPage(p);
        return;
    }
    // keep a few large pages around (per-frame large uploads), release the rest
    u64 pooled = 0;
    for (UploadPage* d : g.freeDedicated) pooled += d->size;
    if (pooled + p->size <= (192ull << 20)) {
        g.freeDedicated.push_back(p);
        return;
    }
    destroyPage(p);
}

inline u64 alignUp(u64 v, u64 a) { return (v + a - 1) / a * a; }

// Memory for data read by GPU work of the current frame (constant / vertex / structured buffers written by the CPU).
UploadAlloc allocDynamic(u64 size, u64 align) {
    FrameData& f = curFrame();
    if (size > kDedicatedThreshold) {
        UploadPage* p = getPage(size);
        p->used = size;
        f.pages.push_back(p);
        return {p->res, 0, p->cpu, p->gpu};
    }
    UploadPage* p = g.dynPage;
    u64 off = p ? alignUp(p->used, align) : 0;
    if (!p || off + size > p->size) {
        p = getPage(size);
        f.pages.push_back(p);
        g.dynPage = p;
        off = 0;
    }
    p->used = off + size;
    return {p->res, off, p->cpu + off, p->gpu + off};
}

// Memory for copy sources (initial resource data): recycled once the submission holding the copy finished.
UploadAlloc allocStaging(u64 size, u64 align) {
    UploadPage* p = g.stagingPage;
    u64 off = p ? alignUp(p->used, align) : 0;
    if (!p || off + size > p->size) {
        p = getPage(size);
        g.stagingOpen.push_back(p);
        g.stagingPage = size > kDedicatedThreshold ? nullptr : p;
        off = 0;
    }
    p->used = off + size;
    g.stagingBytesSinceSubmit += size;
    return {p->res, off, p->cpu + off, p->gpu + off};
}

void onSubmitted(QueueKind q, u64 value) {
    // staging pages written so far are consumed by this submission (copies from them are recorded on the direct queue)
    if (q != QUEUE_DIRECT) return;
    for (UploadPage* p : g.stagingOpen) {
        p->retire[QUEUE_DIRECT] = value;
        p->retire[QUEUE_COMPUTE] = 0;
        p->minFrame = g.frame + kReleaseFrames;
        g.pagesInFlight.push_back(p);
    }
    g.stagingOpen.clear();
    g.stagingPage = nullptr;
    g.stagingBytesSinceSubmit = 0;
}

void recycleCompleted(bool gpuIdle) {
    for (size_t i = 0; i < g.pagesInFlight.size();) {
        UploadPage* p = g.pagesInFlight[i];
        if ((gpuIdle || g.frame >= p->minFrame) && tagReached(p->retire)) {
            recyclePage(p);
            g.pagesInFlight[i] = g.pagesInFlight.back();
            g.pagesInFlight.pop_back();
        } else i++;
    }
    for (size_t i = 0; i < g.deferred.size();) {
        Deferred& d = g.deferred[i];
        if ((gpuIdle || g.frame >= d.minFrame) && tagReached(d.fence)) {
            if (d.obj) d.obj->Release();
            if (d.bindlessSlot != ~0u) g.persistentFree.push_back(d.bindlessSlot);
            if (d.resource) {
                d.resource->d3d = nullptr;
                buryResource(d.resource);
            }
            g.deferred[i] = g.deferred.back();
            g.deferred.pop_back();
        } else i++;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Command allocators
ID3D12CommandAllocator* acquireAllocator(QueueKind q) {
    auto& pool = g.allocators[q];
    u64 done = g.fence[q]->GetCompletedValue();
    for (size_t i = 0; i < pool.size(); i++)
        if (pool[i].fence <= done) {
            ID3D12CommandAllocator* a = pool[i].a;
            pool[i] = pool.back();
            pool.pop_back();
            checkHR(a->Reset(), "ID3D12CommandAllocator::Reset");
            return a;
        }
    ID3D12CommandAllocator* a = nullptr;
    checkHR(g.dev->CreateCommandAllocator(q == QUEUE_DIRECT ? D3D12_COMMAND_LIST_TYPE_DIRECT : D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                          __uuidof(ID3D12CommandAllocator), (void**)&a), "CreateCommandAllocator");
    return a;
}

void waitForFenceValue(QueueKind q, u64 v) {
    if (v == 0 || g.fence[q]->GetCompletedValue() >= v) return;
    checkHR(g.fence[q]->SetEventOnCompletion(v, g.fenceEvent), "SetEventOnCompletion");
    double t0 = Platform::timeSeconds();
    bool warned = false;
    while (WaitForSingleObject(g.fenceEvent, 2000) == WAIT_TIMEOUT) {
        if (g.fence[q]->GetCompletedValue() >= v) break;
        HRESULT reason = g.dev->GetDeviceRemovedReason();
        if (FAILED(reason)) deviceLost("a GPU wait", reason);
        if (!warned && Platform::timeSeconds() - t0 > 60.0) {
            LOG("Direct3D 12: still waiting for the GPU (%s queue fence %llu, completed %llu)", q == QUEUE_DIRECT ? "direct" : "compute",
                (unsigned long long)v, (unsigned long long)g.fence[q]->GetCompletedValue());
            warned = true;
        }
    }
    if (g.fence[q]->GetCompletedValue() == UINT64_MAX) deviceLost("a GPU wait", g.dev->GetDeviceRemovedReason());
}

// ---------------------------------------------------------------------------------------------------------------
// Resource state helpers
inline bool isWriteState(D3D12_RESOURCE_STATES s) {
    return (s & (D3D12_RESOURCE_STATE_RENDER_TARGET | D3D12_RESOURCE_STATE_UNORDERED_ACCESS | D3D12_RESOURCE_STATE_DEPTH_WRITE |
                 D3D12_RESOURCE_STATE_COPY_DEST | D3D12_RESOURCE_STATE_STREAM_OUT | D3D12_RESOURCE_STATE_RESOLVE_DEST)) != 0;
}
inline bool isReadState(D3D12_RESOURCE_STATES s) { return s != D3D12_RESOURCE_STATE_COMMON && !isWriteState(s); }

DXGI_FORMAT srvFormatFor(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_R16_UNORM;
        case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        default: return f;
    }
}
DXGI_FORMAT dsvFormatFor(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_D32_FLOAT;
        case DXGI_FORMAT_R24G8_TYPELESS: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case DXGI_FORMAT_R16_TYPELESS: return DXGI_FORMAT_D16_UNORM;
        case DXGI_FORMAT_R32G8X24_TYPELESS: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default: return f;
    }
}
bool isSrgb(DXGI_FORMAT f) {
    return f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || f == DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
           f == DXGI_FORMAT_BC1_UNORM_SRGB || f == DXGI_FORMAT_BC2_UNORM_SRGB || f == DXGI_FORMAT_BC3_UNORM_SRGB ||
           f == DXGI_FORMAT_BC7_UNORM_SRGB;
}
DXGI_FORMAT typelessFor(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
        default: return f;
    }
}
DXGI_FORMAT unormFor(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
        default: return f;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Views
ViewObj* newView(ResourceObj* r, ViewKind k) {
    ViewObj* v = new ViewObj();
    v->kind = k;
    v->res = r;
    r->views.push_back(v);
    return v;
}

void rangeFromSrv(ViewObj* v, const D3D12_SHADER_RESOURCE_VIEW_DESC& d, const ResourceObj* r) {
    v->mip0 = 0; v->mipCount = r->mips; v->slice0 = 0; v->sliceCount = r->layers;
    auto mipsOf = [&](UINT most, UINT levels) { v->mip0 = most; v->mipCount = levels == UINT_MAX ? r->mips - most : levels; };
    switch (d.ViewDimension) {
        case D3D12_SRV_DIMENSION_TEXTURE2D: mipsOf(d.Texture2D.MostDetailedMip, d.Texture2D.MipLevels); v->sliceCount = 1; break;
        case D3D12_SRV_DIMENSION_TEXTURE2DARRAY:
            mipsOf(d.Texture2DArray.MostDetailedMip, d.Texture2DArray.MipLevels);
            v->slice0 = d.Texture2DArray.FirstArraySlice;
            v->sliceCount = d.Texture2DArray.ArraySize;
            break;
        case D3D12_SRV_DIMENSION_TEXTURE3D: mipsOf(d.Texture3D.MostDetailedMip, d.Texture3D.MipLevels); v->sliceCount = 1; break;
        case D3D12_SRV_DIMENSION_TEXTURECUBE: mipsOf(d.TextureCube.MostDetailedMip, d.TextureCube.MipLevels); v->sliceCount = 6; break;
        case D3D12_SRV_DIMENSION_TEXTURECUBEARRAY:
            mipsOf(d.TextureCubeArray.MostDetailedMip, d.TextureCubeArray.MipLevels);
            v->slice0 = d.TextureCubeArray.First2DArrayFace;
            v->sliceCount = d.TextureCubeArray.NumCubes * 6;
            break;
        default: v->mipCount = 1; v->sliceCount = 1; break;
    }
    if (r->kind == RES_TEXTURE3D) { v->slice0 = 0; v->sliceCount = 1; }
    v->whole = v->mip0 == 0 && v->mipCount >= r->mips && v->slice0 == 0 && v->sliceCount >= r->layers;
}

void rangeFromUav(ViewObj* v, const D3D12_UNORDERED_ACCESS_VIEW_DESC& d, const ResourceObj* r) {
    v->mip0 = 0; v->mipCount = 1; v->slice0 = 0; v->sliceCount = 1;
    switch (d.ViewDimension) {
        case D3D12_UAV_DIMENSION_TEXTURE2D: v->mip0 = d.Texture2D.MipSlice; break;
        case D3D12_UAV_DIMENSION_TEXTURE2DARRAY:
            v->mip0 = d.Texture2DArray.MipSlice;
            v->slice0 = d.Texture2DArray.FirstArraySlice;
            v->sliceCount = d.Texture2DArray.ArraySize;
            break;
        case D3D12_UAV_DIMENSION_TEXTURE3D: v->mip0 = d.Texture3D.MipSlice; break;
        default: break;
    }
    v->whole = r->kind == RES_BUFFER || (v->mip0 == 0 && v->mipCount >= r->mips && v->slice0 == 0 && v->sliceCount >= r->layers);
}

void writeSrv(ViewObj* v) {
    ResourceObj* r = v->res;
    if (r->upload) {
        // upload buffers: the descriptor follows the latest write (null until the first one)
        D3D12_SHADER_RESOURCE_VIEW_DESC d = v->srvDesc;
        if (r->gpuVA && r->uploadPage) {
            u32 elemSize = d.Format == DXGI_FORMAT_UNKNOWN ? d.Buffer.StructureByteStride : 4;
            d.Buffer.FirstElement = r->uploadOffset / elemSize;
            d.Buffer.NumElements = r->uploadSize / elemSize;
            g.dev->CreateShaderResourceView(r->uploadPage, &d, v->cpu);
        } else {
            d.Buffer.FirstElement = 0;
            d.Buffer.NumElements = 0;
            g.dev->CreateShaderResourceView(nullptr, &d, v->cpu);
        }
        return;
    }
    g.dev->CreateShaderResourceView(r->d3d, &v->srvDesc, v->cpu);
}

SRV makeSRV(ResourceObj* r, const D3D12_SHADER_RESOURCE_VIEW_DESC& d) {
    ViewObj* v = newView(r, VIEW_SRV);
    v->srvDesc = d;
    v->format = d.Format;
    if (r->kind == RES_BUFFER) { v->whole = true; v->mipCount = 1; v->sliceCount = 1; }
    else rangeFromSrv(v, d, r);
    v->cpuSlot = g.cpuSrv.alloc(v->cpu);
    writeSrv(v);
    if (!r->upload) {
        v->bindless = allocPersistent();
        g.dev->CopyDescriptorsSimple(1, gpuHeapCpu(v->bindless), v->cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    return v;
}

UAV makeUAV(ResourceObj* r, const D3D12_UNORDERED_ACCESS_VIEW_DESC& d) {
    ViewObj* v = newView(r, VIEW_UAV);
    v->uavDesc = d;
    v->format = d.Format;
    rangeFromUav(v, d, r);
    v->cpuSlot = g.cpuSrv.alloc(v->cpu);
    g.dev->CreateUnorderedAccessView(r->d3d, r->counter ? r->counter->d3d : nullptr, &d, v->cpu);
    v->bindless = allocPersistent();
    g.dev->CopyDescriptorsSimple(1, gpuHeapCpu(v->bindless), v->cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return v;
}

RTV makeRTV(ResourceObj* r, const D3D12_RENDER_TARGET_VIEW_DESC& d) {
    ViewObj* v = newView(r, VIEW_RTV);
    v->format = d.Format;
    v->mip0 = 0; v->mipCount = 1; v->slice0 = 0; v->sliceCount = 1;
    if (d.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2D) v->mip0 = d.Texture2D.MipSlice;
    else if (d.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE2DARRAY) {
        v->mip0 = d.Texture2DArray.MipSlice;
        v->slice0 = d.Texture2DArray.FirstArraySlice;
        v->sliceCount = d.Texture2DArray.ArraySize;
    } else if (d.ViewDimension == D3D12_RTV_DIMENSION_TEXTURE3D) v->mip0 = d.Texture3D.MipSlice;
    v->whole = v->mip0 == 0 && r->mips == 1 && v->slice0 == 0 && v->sliceCount >= r->layers;
    v->cpuSlot = g.cpuRtv.alloc(v->cpu);
    g.dev->CreateRenderTargetView(r->d3d, &d, v->cpu);
    return v;
}

DSV makeDSV(ResourceObj* r, const D3D12_DEPTH_STENCIL_VIEW_DESC& d) {
    ViewObj* v = newView(r, VIEW_DSV);
    v->format = d.Format;
    v->readOnly = (d.Flags & D3D12_DSV_FLAG_READ_ONLY_DEPTH) != 0;
    v->mip0 = 0; v->mipCount = 1; v->slice0 = 0; v->sliceCount = 1;
    if (d.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2D) v->mip0 = d.Texture2D.MipSlice;
    else if (d.ViewDimension == D3D12_DSV_DIMENSION_TEXTURE2DARRAY) {
        v->mip0 = d.Texture2DArray.MipSlice;
        v->slice0 = d.Texture2DArray.FirstArraySlice;
        v->sliceCount = d.Texture2DArray.ArraySize;
    }
    v->whole = v->mip0 == 0 && r->mips == 1 && v->slice0 == 0 && v->sliceCount >= r->layers;
    v->cpuSlot = g.cpuDsv.alloc(v->cpu);
    g.dev->CreateDepthStencilView(r->d3d, &d, v->cpu);
    return v;
}

// Null render target for gaps in a render target list (created when first needed).
D3D12_CPU_DESCRIPTOR_HANDLE nullRTV() {
    if (!g.nullRtv.ptr) {
        D3D12_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        g.cpuRtv.alloc(g.nullRtv);
        g.dev->CreateRenderTargetView(nullptr, &rv, g.nullRtv);
    }
    return g.nullRtv;
}

void releaseView(ViewObj* v) {
    if (!v) return;
    if (ctx) ctx->onViewRelease(v);
    for (Context* c : g.asyncPool) c->onViewRelease(v);
    switch (v->kind) {
        case VIEW_SRV:
        case VIEW_UAV:
            g.cpuSrv.free(v->cpuSlot);
            if (!v->res->upload) deferPersistentFree(v->bindless);
            break;
        case VIEW_RTV: g.cpuRtv.free(v->cpuSlot); break;
        case VIEW_DSV: g.cpuDsv.free(v->cpuSlot); break;
    }
    v->dead = true;
    v->cpu = D3D12_CPU_DESCRIPTOR_HANDLE{0};
    g.deadViews.push_back(v);
    while (g.deadViews.size() > kQuarantine) {
        delete g.deadViews.front();
        g.deadViews.pop_front();
    }
}

void buryResource(ResourceObj* r) {
    r->dead = true;
    g.deadResources.push_back(r);
    while (g.deadResources.size() > kQuarantine) {
        delete g.deadResources.front();
        g.deadResources.pop_front();
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Null descriptors (their kind must match what the shader declares at that slot; reflection reports the
// dimension of UAVs with the D3D_SRV_DIMENSION values too)
D3D12_CPU_DESCRIPTOR_HANDLE nullSRV(u32 type, u32 dim, u32 stride) {
    if (type == D3D_SIT_STRUCTURED) dim = D3D_SRV_DIMENSION_BUFFER;
    else stride = 0;
    if (type == D3D_SIT_BYTEADDRESS) dim = D3D_SRV_DIMENSION_BUFFEREX;
    if (dim == 0) dim = D3D_SRV_DIMENSION_TEXTURE2D;
    u32 key = (type << 24) | (dim << 16) | (stride & 0xffff);
    auto it = g.nullSrvs.find(key);
    if (it != g.nullSrvs.end()) return it->second;
    D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
    d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    switch (dim) {
        case D3D_SRV_DIMENSION_BUFFER:
        case D3D_SRV_DIMENSION_BUFFEREX:
            d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            if (type == D3D_SIT_STRUCTURED) {
                d.Format = DXGI_FORMAT_UNKNOWN;
                d.Buffer.StructureByteStride = Max(stride, 4u);
            } else if (type == D3D_SIT_BYTEADDRESS) {
                d.Format = DXGI_FORMAT_R32_TYPELESS;
                d.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            } else d.Format = DXGI_FORMAT_R32_UINT;
            break;
        case D3D_SRV_DIMENSION_TEXTURE1D: d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D; d.Texture1D.MipLevels = 1; break;
        case D3D_SRV_DIMENSION_TEXTURE1DARRAY: d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY; d.Texture1DArray.MipLevels = 1; d.Texture1DArray.ArraySize = 1; break;
        case D3D_SRV_DIMENSION_TEXTURE2DARRAY: d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY; d.Texture2DArray.MipLevels = 1; d.Texture2DArray.ArraySize = 1; break;
        case D3D_SRV_DIMENSION_TEXTURE3D: d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; d.Texture3D.MipLevels = 1; break;
        case D3D_SRV_DIMENSION_TEXTURECUBE: d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; d.TextureCube.MipLevels = 1; break;
        case D3D_SRV_DIMENSION_TEXTURECUBEARRAY: d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY; d.TextureCubeArray.MipLevels = 1; d.TextureCubeArray.NumCubes = 1; break;
        default: d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; d.Texture2D.MipLevels = 1; break;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    g.cpuSrv.alloc(h);
    g.dev->CreateShaderResourceView(nullptr, &d, h);
    g.nullSrvs[key] = h;
    return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE nullUAV(u32 type, u32 dim, u32 stride) {
    bool structured = type == D3D_SIT_UAV_RWSTRUCTURED || type == D3D_SIT_UAV_APPEND_STRUCTURED || type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
                      type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER;
    if (structured || type == D3D_SIT_UAV_RWBYTEADDRESS) dim = D3D_SRV_DIMENSION_BUFFER;
    if (!structured) stride = 0;
    if (dim == 0) dim = D3D_SRV_DIMENSION_BUFFER;
    u32 key = (type << 24) | (dim << 16) | (stride & 0xffff);
    auto it = g.nullUavs.find(key);
    if (it != g.nullUavs.end()) return it->second;
    D3D12_UNORDERED_ACCESS_VIEW_DESC d = {};
    d.Format = DXGI_FORMAT_R32_UINT;
    switch (dim) {
        case D3D_SRV_DIMENSION_TEXTURE1D: d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case D3D_SRV_DIMENSION_TEXTURE1DARRAY: d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY; d.Texture1DArray.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case D3D_SRV_DIMENSION_TEXTURE2D: d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case D3D_SRV_DIMENSION_TEXTURE2DARRAY: d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; d.Texture2DArray.ArraySize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        case D3D_SRV_DIMENSION_TEXTURE3D: d.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; d.Texture3D.WSize = 1; d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; break;
        default:
            d.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            if (structured) {
                d.Format = DXGI_FORMAT_UNKNOWN;
                d.Buffer.StructureByteStride = Max(stride, 4u);
            } else if (type == D3D_SIT_UAV_RWBYTEADDRESS) {
                d.Format = DXGI_FORMAT_R32_TYPELESS;
                d.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            }
            break;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE h;
    g.cpuSrv.alloc(h);
    g.dev->CreateUnorderedAccessView(nullptr, nullptr, &d, h);
    g.nullUavs[key] = h;
    return h;
}

// ---------------------------------------------------------------------------------------------------------------
// Resource creation
ResourceObj* newResource(ResourceKind k) {
    ResourceObj* r = new ResourceObj();
    r->kind = k;
    return r;
}

void setName(ResourceObj* r, const char* name) {
    if (!name) return;
    r->name = name;
    if (!r->d3d) return;
    wchar_t w[128];
    MultiByteToWideChar(CP_UTF8, 0, name, -1, w, 128);
    r->d3d->SetName(w);
}

// Records the upload of one texture subresource from CPU memory on the direct context.
void uploadSubresource(ResourceObj* r, u32 sub, const void* data, u32 rowPitch) {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
    UINT rows = 0;
    UINT64 rowBytes = 0, total = 0;
    g.dev->GetCopyableFootprints(&r->desc, sub, 1, 0, &fp, &rows, &rowBytes, &total);
    UploadAlloc a = allocStaging(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    u32 depth = fp.Footprint.Depth;
    size_t copyBytes = (size_t)Min<UINT64>(rowBytes, rowPitch);
    for (u32 z = 0; z < depth; z++)
        for (UINT y = 0; y < rows; y++)
            memcpy(a.cpu + ((size_t)z * rows + y) * fp.Footprint.RowPitch, (const u8*)data + ((size_t)z * rows + y) * rowPitch, copyBytes);
    fp.Offset = a.offset;
    ctx->transition(r, D3D12_RESOURCE_STATE_COPY_DEST, sub);
    ctx->flushBarriers();
    D3D12_TEXTURE_COPY_LOCATION dst = {}, src = {};
    dst.pResource = r->d3d;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = sub;
    src.pResource = a.res;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = fp;
    ctx->list()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    ctx->anyWork = true;
    if (g.stagingBytesSinceSubmit > kStagingSubmitBytes) ctx->submit();
}

D3D12_RESOURCE_DESC textureResourceDesc(const TextureDesc& d, int mips) {
    D3D12_RESOURCE_DESC rd = {};
    rd.Dimension = d.is3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = (UINT64)d.width;
    rd.Height = (UINT)d.height;
    rd.DepthOrArraySize = (UINT16)(d.is3D ? d.depth : d.layers);
    rd.MipLevels = (UINT16)mips;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    DXGI_FORMAT fmt = d.format;
    bool srgbView = isSrgb(d.srvFormat) || isSrgb(fmt);
    bool uav = (d.flags & (TEX_UAV | TEX_GENMIPS)) != 0;
    // sRGB formats cannot be UAVs: typeless resource with an sRGB SRV and a UNORM UAV
    if (uav && srgbView) fmt = typelessFor(fmt);
    rd.Format = fmt;
    if (d.flags & TEX_RTV) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (d.flags & TEX_DSV) {
        rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (!(d.flags & TEX_SRV)) rd.Flags |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }
    if (uav) rd.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    return rd;
}

void createTextureViews(Texture& t, ResourceObj* r, const TextureDesc& d) {
    DXGI_FORMAT resFmt = r->desc.Format;
    DXGI_FORMAT srvFmt = d.srvFormat != DXGI_FORMAT_UNKNOWN ? d.srvFormat : srvFormatFor(d.format);
    DXGI_FORMAT uavFmt = d.uavFormat != DXGI_FORMAT_UNKNOWN ? d.uavFormat : unormFor(srvFormatFor(d.format));
    DXGI_FORMAT rtvFmt = d.rtvFormat != DXGI_FORMAT_UNKNOWN ? d.rtvFormat : d.format;
    DXGI_FORMAT dsvFmt = dsvFormatFor(resFmt);
    t.srgb = isSrgb(srvFmt);
    bool cube = (d.flags & TEX_CUBE) != 0;
    int mips = t.mips, layers = t.layers;
    if (d.flags & (TEX_SRV | TEX_GENMIPS)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Format = srvFmt;
        if (d.is3D) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D; sv.Texture3D.MipLevels = (UINT)mips; }
        else if (cube) {
            if (layers > 6) { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY; sv.TextureCubeArray.MipLevels = (UINT)mips; sv.TextureCubeArray.NumCubes = (UINT)layers / 6; }
            else { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE; sv.TextureCube.MipLevels = (UINT)mips; }
        } else if (layers > 1) {
            sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            sv.Texture2DArray.MipLevels = (UINT)mips;
            sv.Texture2DArray.ArraySize = (UINT)layers;
        } else { sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; sv.Texture2D.MipLevels = (UINT)mips; }
        t.srv = makeSRV(r, sv);
    }
    if (d.flags & TEX_RTV) {
        D3D12_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = rtvFmt;
        if (layers > 1) { rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; rv.Texture2DArray.ArraySize = (UINT)layers; }
        else rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        t.rtv = makeRTV(r, rv);
        if (d.flags & TEX_SLICE_RTVS)
            for (int i = 0; i < layers; i++) {
                D3D12_RENDER_TARGET_VIEW_DESC r1 = {};
                r1.Format = rtvFmt;
                r1.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                r1.Texture2DArray.FirstArraySlice = (UINT)i;
                r1.Texture2DArray.ArraySize = 1;
                t.sliceRtvs.push_back(makeRTV(r, r1));
            }
    }
    if (d.flags & TEX_DSV) {
        D3D12_DEPTH_STENCIL_VIEW_DESC dv = {};
        dv.Format = dsvFmt;
        if (layers > 1) { dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY; dv.Texture2DArray.ArraySize = (UINT)layers; }
        else dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        t.dsv = makeDSV(r, dv);
        if (d.flags & TEX_DSV_READONLY) {
            D3D12_DEPTH_STENCIL_VIEW_DESC ro = dv;
            ro.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
            t.dsvRO = makeDSV(r, ro);
        }
        if (d.flags & TEX_SLICE_RTVS)
            for (int i = 0; i < layers; i++) {
                D3D12_DEPTH_STENCIL_VIEW_DESC d1 = {};
                d1.Format = dsvFmt;
                d1.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                d1.Texture2DArray.FirstArraySlice = (UINT)i;
                d1.Texture2DArray.ArraySize = 1;
                t.sliceDsvs.push_back(makeDSV(r, d1));
            }
    }
    if (d.flags & TEX_UAV) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.Format = uavFmt;
        if (d.is3D) { uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; uv.Texture3D.WSize = (UINT)t.depth; }
        else if (layers > 1) { uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; uv.Texture2DArray.ArraySize = (UINT)layers; }
        else uv.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        t.uav = makeUAV(r, uv);
        if (d.flags & TEX_MIP_UAVS) {
            for (int m = 0; m < mips; m++) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC u1 = uv;
                D3D12_SHADER_RESOURCE_VIEW_DESC s1 = {};
                s1.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                s1.Format = srvFmt;
                if (d.is3D) {
                    u1.Texture3D.MipSlice = (UINT)m;
                    u1.Texture3D.WSize = (UINT)Max(1, t.depth >> m);
                    s1.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                    s1.Texture3D.MostDetailedMip = (UINT)m;
                    s1.Texture3D.MipLevels = 1;
                } else if (layers > 1) {
                    u1.Texture2DArray.MipSlice = (UINT)m;
                    s1.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                    s1.Texture2DArray.MostDetailedMip = (UINT)m;
                    s1.Texture2DArray.MipLevels = 1;
                    s1.Texture2DArray.ArraySize = (UINT)layers;
                } else {
                    u1.Texture2D.MipSlice = (UINT)m;
                    s1.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    s1.Texture2D.MostDetailedMip = (UINT)m;
                    s1.Texture2D.MipLevels = 1;
                }
                t.mipUavs.push_back(makeUAV(r, u1));
                t.mipSrvs.push_back((d.flags & TEX_SRV) ? makeSRV(r, s1) : nullptr);
            }
        }
    }
    if (d.flags & TEX_GENMIPS) {
        // mip generator views: every mip as a 2D array (plain 2D textures and cubes included)
        for (int m = 0; m < mips; m++) {
            D3D12_SHADER_RESOURCE_VIEW_DESC s1 = {};
            s1.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            s1.Format = srvFmt;
            s1.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            s1.Texture2DArray.MostDetailedMip = (UINT)m;
            s1.Texture2DArray.MipLevels = 1;
            s1.Texture2DArray.ArraySize = (UINT)layers;
            t.genSrvs.push_back(makeSRV(r, s1));
            D3D12_UNORDERED_ACCESS_VIEW_DESC u1 = {};
            u1.Format = uavFmt;
            u1.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            u1.Texture2DArray.MipSlice = (UINT)m;
            u1.Texture2DArray.ArraySize = (UINT)layers;
            t.genUavs.push_back(makeUAV(r, u1));
        }
    }
}

Texture createTextureIn(const TextureDesc& dIn, const void* initData, int initPitch, Heap heap, u64 heapOffset) {
    TextureDesc d = dIn;
    if (d.depth > 1) d.is3D = true;
    if (d.flags & TEX_GENMIPS) {
        if (d.is3D) FatalError("Direct3D 12: TEX_GENMIPS supports 2D textures and arrays only");
        d.flags |= TEX_SRV;
    }
    Texture t;
    int mips = d.mips <= 0 ? mipCount(d.width, d.height) : d.mips;
    if (d.is3D && d.mips <= 0) {
        int m = 1, w = d.width, h = d.height, z = d.depth;
        while (w > 1 || h > 1 || z > 1) { w = Max(1, w / 2); h = Max(1, h / 2); z = Max(1, z / 2); m++; }
        mips = m;
    }
    t.width = d.width; t.height = d.height; t.depth = d.is3D ? d.depth : 1; t.mips = mips; t.layers = d.is3D ? 1 : d.layers;
    t.format = d.format; t.is3D = d.is3D;
    ResourceObj* r = newResource(d.is3D ? RES_TEXTURE3D : RES_TEXTURE2D);
    r->flags = d.flags;
    r->desc = textureResourceDesc(d, mips);
    r->mips = (u32)mips;
    r->layers = (u32)t.layers;
    r->depth = (u32)t.depth;
    r->format = d.format;
    r->numSub = r->mips * r->layers;
    D3D12_CLEAR_VALUE cv = {};
    const D3D12_CLEAR_VALUE* pcv = nullptr;
    if (d.useClear && (d.flags & (TEX_RTV | TEX_DSV))) {
        if (d.flags & TEX_DSV) {
            cv.Format = dsvFormatFor(r->desc.Format);
            cv.DepthStencil.Depth = d.clear[0];
        } else {
            cv.Format = d.rtvFormat != DXGI_FORMAT_UNKNOWN ? d.rtvFormat : d.format;
            memcpy(cv.Color, d.clear, sizeof(cv.Color));
        }
        pcv = &cv;
    }
    HRESULT hr;
    if (heap) {
        hr = g.dev->CreatePlacedResource(heap->d3d, heapOffset, &r->desc, D3D12_RESOURCE_STATE_COMMON, pcv, __uuidof(ID3D12Resource), (void**)&r->d3d);
        r->heap = heap;
        r->heapOffset = heapOffset;
    } else {
        D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
        hr = g.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &r->desc, D3D12_RESOURCE_STATE_COMMON, pcv, __uuidof(ID3D12Resource), (void**)&r->d3d);
    }
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG) deviceLost("texture creation", hr);
        FatalError("Direct3D 12: texture creation %dx%dx%d fmt %d flags %x failed (%08lx)", d.width, d.height, d.is3D ? d.depth : d.layers,
                   (int)d.format, d.flags, (unsigned long)hr);
    }
    r->state = D3D12_RESOURCE_STATE_COMMON;
    t.res = r;
    setName(r, d.name);
    createTextureViews(t, r, d);
    if (initData) uploadSubresource(r, 0, initData, (u32)initPitch);
    return t;
}

Buffer createBufferIn(u32 size, u32 stride, u32 flags, const void* init, const char* name, Heap heap, u64 heapOffset);

// Writes an upload buffer's contents into fresh upload memory of the current frame.
void writeUpload(ResourceObj* r, const void* data, u32 size) {
    u64 allocSize = size;
    u64 align = 16;
    if (r->flags & BUF_CONSTANT) {
        allocSize = alignUp(Max(size, r->size), 256);
        align = 256;
    } else if (r->flags & BUF_STRUCTURED) {
        u64 s = r->stride ? r->stride : 4;
        while (align % s) align += 16;
    }
    UploadAlloc a = allocDynamic(allocSize, align);
    memcpy(a.cpu, data, size);
    r->uploadPage = a.res;
    r->uploadOffset = a.offset;
    r->uploadCpu = a.cpu;
    r->uploadSize = (r->flags & BUF_CONSTANT) ? (u32)allocSize : size;
    r->gpuVA = a.gpu;
    r->version++;
    if (r->uploadFrame != g.frame) curFrame().written.push_back(r);
    r->uploadFrame = g.frame;
    for (ViewObj* v : r->views)
        if (v->kind == VIEW_SRV) {
            writeSrv(v);
            u32 slot = allocRing(1);
            g.dev->CopyDescriptorsSimple(1, gpuHeapCpu(slot), v->cpu, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            v->bindless = slot;
            if (ctx) ctx->markSRVTablesDirty();
            for (Context* c : g.asyncPool) c->markSRVTablesDirty();
        }
}

// A frame's GPU work finished: recycle its upload pages and ring space. Upload buffers keep their contents until
// rewritten, so the latest write of a buffer not rewritten since moves into the current frame's pages first.
// Called once the GPU finished frame f. Work submitted after it (and, when allocRing retires early, this frame's
// commands so far) can still read f's upload memory and descriptor ring slots through upload buffers that were not
// rewritten since: those buffers are copied into the current frame here, and f's pages and ring space are reused only
// after that later work has finished too (pages: on the fences of the next submission; ring: at the next retirement).
void retireFrame(FrameData& f) {
    if (!f.number) return;
    std::vector<UploadPage*> pages;
    pages.swap(f.pages);
    std::vector<ResourceObj*> written;
    written.swap(f.written);
    u64 number = f.number;
    g.ringUsed -= g.ringPendingFree;
    g.ringPendingFree = f.ringUsed;
    f.ringUsed = 0;
    f.number = 0;
    for (UploadPage* p : pages)
        if (p == g.dynPage) g.dynPage = nullptr;
    for (ResourceObj* r : written)
        if (r->uploadFrame == number && r->uploadCpu && r->uploadSize) writeUpload(r, r->uploadCpu, r->uploadSize);
    u64 tag[QUEUE_COUNT];
    retireTag(tag);
    for (UploadPage* p : pages) {
        memcpy(p->retire, tag, sizeof(tag));
        p->minFrame = g.frame + kReleaseFrames;
        g.pagesInFlight.push_back(p);
    }
}

void createBackbufferViews() {
    for (u32 i = 0; i < DeviceState::kBackbuffers; i++) {
        ResourceObj* r = newResource(RES_TEXTURE2D);
        checkHR(g.swap->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&r->d3d), "IDXGISwapChain::GetBuffer");
        r->desc = r->d3d->GetDesc();
        r->format = DXGI_FORMAT_R8G8B8A8_UNORM;
        r->state = D3D12_RESOURCE_STATE_PRESENT;
        r->name = "backbuffer";
        D3D12_RENDER_TARGET_VIEW_DESC rv = {};
        rv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        g.bb[i] = r;
        g.bbRtv[i] = makeRTV(r, rv);
    }
}

void releaseBackbuffers() {
    for (u32 i = 0; i < DeviceState::kBackbuffers; i++) {
        if (!g.bb[i]) continue;
        if (ctx) ctx->onRelease(g.bb[i]);
        for (ViewObj* v : g.bb[i]->views) releaseView(v);
        g.bb[i]->views.clear();
        g.bb[i]->d3d->Release();
        delete g.bb[i];
        g.bb[i] = nullptr;
        g.bbRtv[i] = nullptr;
    }
}

D3D12_SAMPLER_DESC samplerDesc(D3D12_FILTER f, D3D12_TEXTURE_ADDRESS_MODE a, u32 aniso) {
    D3D12_SAMPLER_DESC s = {};
    s.Filter = f;
    s.AddressU = s.AddressV = s.AddressW = a;
    s.MipLODBias = 0.f;
    s.MaxAnisotropy = aniso;
    s.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    s.MinLOD = 0.f;
    s.MaxLOD = D3D12_FLOAT32_MAX;
    return s;
}

// The fixed sampler set s0..s6 (shaders/common.hlsli) in the shader-visible sampler heap.
void createSamplers() {
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, kSamplerHeapSize, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    checkHR(g.dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g.samplerHeap), "CreateDescriptorHeap (samplers)");
    D3D12_SAMPLER_DESC s[7] = {
        samplerDesc(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1),
        samplerDesc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 1),
        samplerDesc(D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 1),
        samplerDesc(D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 8),
        samplerDesc(D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, 1),
        samplerDesc(D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_WRAP, 1),
        samplerDesc(D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, 8),
    };
    s[4].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    s[4].BorderColor[0] = s[4].BorderColor[1] = s[4].BorderColor[2] = s[4].BorderColor[3] = 1.f;
    u32 inc = g.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    D3D12_CPU_DESCRIPTOR_HANDLE h = g.samplerHeap->GetCPUDescriptorHandleForHeapStart();
    for (int i = 0; i < 7; i++) {
        g.dev->CreateSampler(&s[i], h);
        h.ptr += inc;
    }
    g.samplerHeapGpu = g.samplerHeap->GetGPUDescriptorHandleForHeapStart();
}

void createRootSignatures() {
    D3D12_DESCRIPTOR_RANGE samplerRange = {D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 7, 0, 0, 0};
    // bindless: every unbounded range starts at the heap start (overlapping ranges in separate register spaces)
    std::vector<D3D12_DESCRIPTOR_RANGE> bindless;
    for (u32 s = 1; s <= kBindlessSRVSpaces; s++) bindless.push_back({D3D12_DESCRIPTOR_RANGE_TYPE_SRV, UINT_MAX, 0, s, 0});
    if (g.bindingTier >= D3D12_RESOURCE_BINDING_TIER_3)
        for (u32 s = 1; s <= kBindlessUAVSpaces; s++) bindless.push_back({D3D12_DESCRIPTOR_RANGE_TYPE_UAV, UINT_MAX, 0, s, 0});
    D3D12_DESCRIPTOR_RANGE localRange = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kLocalSRVSlots, 0, 0, 0};
    D3D12_DESCRIPTOR_RANGE globalRange = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kMaxSRVSlots - kLocalSRVSlots, kLocalSRVSlots, 0, 0};
    D3D12_DESCRIPTOR_RANGE uavRange = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, kMaxUAVSlots, 0, 0, 0};
    auto table = [](D3D12_ROOT_PARAMETER& p, const D3D12_DESCRIPTOR_RANGE* r, u32 n, D3D12_SHADER_VISIBILITY vis) {
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        p.DescriptorTable.NumDescriptorRanges = n;
        p.DescriptorTable.pDescriptorRanges = r;
        p.ShaderVisibility = vis;
    };
    auto cbv = [](D3D12_ROOT_PARAMETER& p, u32 reg, D3D12_SHADER_VISIBILITY vis) {
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        p.Descriptor.ShaderRegister = reg;
        p.Descriptor.RegisterSpace = 0;
        p.ShaderVisibility = vis;
    };
    auto consts = [](D3D12_ROOT_PARAMETER& p) {
        p.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        p.Constants.ShaderRegister = 0;
        p.Constants.RegisterSpace = kRootConstantSpace;
        p.Constants.Num32BitValues = kRootConstants;
        p.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    };
    {
        D3D12_ROOT_PARAMETER p[GRP_COUNT] = {};
        for (u32 i = 0; i < kMaxCBSlots; i++) {
            cbv(p[GRP_CB_VS + i], i, D3D12_SHADER_VISIBILITY_VERTEX);
            cbv(p[GRP_CB_PS + i], i, D3D12_SHADER_VISIBILITY_PIXEL);
        }
        table(p[GRP_SRV_VS_LOCAL], &localRange, 1, D3D12_SHADER_VISIBILITY_VERTEX);
        table(p[GRP_SRV_VS_GLOBAL], &globalRange, 1, D3D12_SHADER_VISIBILITY_VERTEX);
        table(p[GRP_SRV_PS_LOCAL], &localRange, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        table(p[GRP_SRV_PS_GLOBAL], &globalRange, 1, D3D12_SHADER_VISIBILITY_PIXEL);
        consts(p[GRP_ROOT_CONSTANTS]);
        table(p[GRP_BINDLESS], bindless.data(), (u32)bindless.size(), D3D12_SHADER_VISIBILITY_ALL);
        table(p[GRP_SAMPLERS], &samplerRange, 1, D3D12_SHADER_VISIBILITY_ALL);
        D3D12_ROOT_SIGNATURE_DESC rd = {GRP_COUNT, p, 0, nullptr,
                                        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                                            D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                                            D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS};
        g.rsGraphics = createRootSignature(rd);
    }
    {
        D3D12_ROOT_PARAMETER p[CRP_COUNT] = {};
        for (u32 i = 0; i < kMaxCBSlots; i++) cbv(p[CRP_CB + i], i, D3D12_SHADER_VISIBILITY_ALL);
        table(p[CRP_SRV_LOCAL], &localRange, 1, D3D12_SHADER_VISIBILITY_ALL);
        table(p[CRP_SRV_GLOBAL], &globalRange, 1, D3D12_SHADER_VISIBILITY_ALL);
        table(p[CRP_UAV], &uavRange, 1, D3D12_SHADER_VISIBILITY_ALL);
        consts(p[CRP_ROOT_CONSTANTS]);
        table(p[CRP_BINDLESS], bindless.data(), (u32)bindless.size(), D3D12_SHADER_VISIBILITY_ALL);
        table(p[CRP_SAMPLERS], &samplerRange, 1, D3D12_SHADER_VISIBILITY_ALL);
        D3D12_ROOT_SIGNATURE_DESC rd = {CRP_COUNT, p, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
        g.rsCompute = createRootSignature(rd);
    }
}

ID3D12CommandSignature* makeCommandSignature(D3D12_INDIRECT_ARGUMENT_TYPE type, u32 rootConstants, bool compute, u32& stride) {
    D3D12_INDIRECT_ARGUMENT_DESC args[2] = {};
    u32 n = 0;
    stride = 0;
    if (rootConstants) {
        args[n].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
        args[n].Constant.RootParameterIndex = compute ? (u32)CRP_ROOT_CONSTANTS : (u32)GRP_ROOT_CONSTANTS;
        args[n].Constant.DestOffsetIn32BitValues = 0;
        args[n].Constant.Num32BitValuesToSet = rootConstants;
        stride += rootConstants * 4;
        n++;
    }
    args[n].Type = type;
    n++;
    stride += type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW ? (u32)sizeof(D3D12_DRAW_ARGUMENTS)
                                                        : (type == D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED ? (u32)sizeof(D3D12_DRAW_INDEXED_ARGUMENTS)
                                                                                                              : (u32)sizeof(D3D12_DISPATCH_ARGUMENTS));
    D3D12_COMMAND_SIGNATURE_DESC d = {};
    d.ByteStride = stride;
    d.NumArgumentDescs = n;
    d.pArgumentDescs = args;
    ID3D12CommandSignature* s = nullptr;
    checkHR(g.dev->CreateCommandSignature(&d, rootConstants ? (compute ? g.rsCompute : g.rsGraphics) : nullptr,
                                          __uuidof(ID3D12CommandSignature), (void**)&s), "CreateCommandSignature");
    return s;
}

void createStates() {
    RasterDesc rs;
    states.cullBack = createRasterState(rs);
    rs.cull = CULL_FRONT;
    states.cullFront = createRasterState(rs);
    rs.cull = CULL_NONE;
    states.cullNone = createRasterState(rs);
    rs.scissor = true;
    states.cullNoneScissor = createRasterState(rs);
    rs.scissor = false;
    rs.wireframe = true;
    states.wireframe = createRasterState(rs);
    rs.wireframe = false;
    rs.cull = CULL_NONE;
    rs.depthBias = 0;
    rs.slopeBias = 1.5f;
    rs.biasClamp = 0.02f;
    rs.depthClip = false;   // pancaking for shadow casters behind the near plane
    states.shadowBias = createRasterState(rs);

    DepthDesc ds;
    states.depthGreaterWrite = createDepthState(ds);
    ds.write = false;
    ds.func = CMP_GREATER_EQUAL;
    states.depthGreaterEqualNoWrite = createDepthState(ds);
    ds.func = CMP_EQUAL;
    states.depthEqualNoWrite = createDepthState(ds);
    ds.write = true;
    ds.func = CMP_LESS_EQUAL;
    states.depthLessWrite = createDepthState(ds);
    ds.test = false;
    ds.write = false;
    states.depthOff = createDepthState(ds);

    BlendDesc bd;
    states.opaque = createBlendState(bd);
    bd.rt[0] = {true, BLEND_SRC_ALPHA, BLEND_INV_SRC_ALPHA, BLENDOP_ADD, BLEND_ONE, BLEND_INV_SRC_ALPHA, BLENDOP_ADD, WRITE_ALL};
    states.alpha = createBlendState(bd);
    bd.rt[0].src = BLEND_ONE;
    states.premultiplied = createBlendState(bd);
    bd.rt[0] = {true, BLEND_ONE, BLEND_ONE, BLENDOP_ADD, BLEND_ONE, BLEND_ONE, BLENDOP_ADD, WRITE_ALL};
    states.additive = createBlendState(bd);
    BlendDesc nb;
    nb.rt[0].writeMask = 0;
    states.noColorWrite = createBlendState(nb);
    // pipeline defaults for unset state (as the D3D runtime defines them)
    g.defaultBlend = states.opaque;
    RasterDesc dr;
    dr.frontCCW = false;
    g.defaultRaster = createRasterState(dr);
    DepthDesc dd;
    dd.func = CMP_LESS;
    g.defaultDepth = createDepthState(dd);
}

Buffer createBufferIn(u32 size, u32 stride, u32 flags, const void* init, const char* name, Heap heap, u64 heapOffset) {
    Buffer b;
    b.size = size;
    b.stride = stride;
    b.flags = flags;
    if (size == 0) return b;
    ResourceObj* r = newResource(RES_BUFFER);
    r->flags = flags;
    r->size = size;
    r->stride = stride;
    if (name) r->name = name;
    if ((flags & BUF_DYNAMIC) && !heap) {
        if (flags & BUF_UAV) FatalError("Direct3D 12: dynamic buffers cannot have UAVs");
        r->upload = true;
        r->liveSlot = g.liveUploads.size();
        g.liveUploads.push_back(r);
    } else {
        u64 bytes = size;
        if (flags & BUF_CONSTANT) bytes = alignUp(bytes, 256);
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = bytes;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (flags & BUF_UAV) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        r->desc = d;
        D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_STATES initState = D3D12_RESOURCE_STATE_COMMON;
        if (flags & BUF_READBACK) {
            hp.Type = D3D12_HEAP_TYPE_READBACK;
            initState = D3D12_RESOURCE_STATE_COPY_DEST;
            r->readback = true;
        }
        HRESULT hr = heap ? g.dev->CreatePlacedResource(heap->d3d, heapOffset, &d, initState, nullptr, __uuidof(ID3D12Resource), (void**)&r->d3d)
                          : g.dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, initState, nullptr, __uuidof(ID3D12Resource), (void**)&r->d3d);
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_HUNG) deviceLost("buffer creation", hr);
            FatalError("Direct3D 12: buffer creation of %u bytes (flags %x) failed (%08lx)", size, flags, (unsigned long)hr);
        }
        if (heap) {
            r->heap = heap;
            r->heapOffset = heapOffset;
        }
        r->state = initState;
        r->gpuVA = r->d3d->GetGPUVirtualAddress();
        setName(r, name);
        if (r->readback) {
            D3D12_RANGE none = {0, 0};
            if (FAILED(r->d3d->Map(0, &none, (void**)&r->uploadCpu))) r->uploadCpu = nullptr;
        }
        if (flags & BUF_APPEND) {
            Buffer c = createBufferIn(4, 4, BUF_UAV, nullptr, "uav counter", nullptr, 0);
            r->counter = c.buf;
        }
    }
    b.buf = r;
    if ((flags & (BUF_SRV | BUF_STRUCTURED | BUF_RAW)) && !(flags & BUF_READBACK)) {
        D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        if (flags & BUF_RAW) {
            sv.Format = DXGI_FORMAT_R32_TYPELESS;
            sv.Buffer.NumElements = size / 4;
            sv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
        } else if (flags & BUF_STRUCTURED) {
            sv.Format = DXGI_FORMAT_UNKNOWN;
            sv.Buffer.NumElements = size / stride;
            sv.Buffer.StructureByteStride = stride;
        } else {
            sv.Format = DXGI_FORMAT_R32_UINT;
            sv.Buffer.NumElements = size / 4;
        }
        b.srv = makeSRV(r, sv);
    }
    if (flags & BUF_UAV) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv = {};
        uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        if (flags & BUF_RAW) {
            uv.Format = DXGI_FORMAT_R32_TYPELESS;
            uv.Buffer.NumElements = size / 4;
            uv.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
        } else if (flags & BUF_STRUCTURED) {
            uv.Format = DXGI_FORMAT_UNKNOWN;
            uv.Buffer.NumElements = size / stride;
            uv.Buffer.StructureByteStride = stride;
        } else {
            uv.Format = DXGI_FORMAT_R32_UINT;
            uv.Buffer.NumElements = size / 4;
        }
        b.uav = makeUAV(r, uv);
    }
    if (init) {
        if (r->upload) writeUpload(r, init, size);
        else if (!r->readback) {
            UploadAlloc a = allocStaging(size, 16);
            memcpy(a.cpu, init, size);
            ctx->transition(r, D3D12_RESOURCE_STATE_COPY_DEST);
            ctx->flushBarriers();
            ctx->list()->CopyBufferRegion(r->d3d, 0, a.res, a.offset, size);
            ctx->anyWork = true;
            if (g.stagingBytesSinceSubmit > kStagingSubmitBytes) ctx->submit();
        }
    }
    return b;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// State objects
BlendState createBlendState(const BlendDesc& d) {
    for (BlendStateObj* s : g.blendStates)
        if (memcmp(&s->desc, &d, sizeof(BlendDesc)) == 0) return s;
    BlendStateObj* s = new BlendStateObj();
    memset((void*)s, 0, sizeof(*s));
    s->desc = d;
    s->d3d.AlphaToCoverageEnable = d.alphaToCoverage;
    s->d3d.IndependentBlendEnable = d.independent;
    for (int i = 0; i < 8; i++) {
        const RTBlend& b = d.rt[d.independent ? i : 0];
        D3D12_RENDER_TARGET_BLEND_DESC& o = s->d3d.RenderTarget[i];
        o.BlendEnable = b.enable;
        o.LogicOpEnable = FALSE;
        o.SrcBlend = (D3D12_BLEND)b.src;
        o.DestBlend = (D3D12_BLEND)b.dst;
        o.BlendOp = (D3D12_BLEND_OP)b.op;
        o.SrcBlendAlpha = (D3D12_BLEND)b.srcA;
        o.DestBlendAlpha = (D3D12_BLEND)b.dstA;
        o.BlendOpAlpha = (D3D12_BLEND_OP)b.opA;
        o.LogicOp = D3D12_LOGIC_OP_NOOP;
        o.RenderTargetWriteMask = b.writeMask;
    }
    s->id = (u32)g.blendStates.size() + 1;
    g.blendStates.push_back(s);
    return s;
}

RasterState createRasterState(const RasterDesc& d) {
    for (RasterStateObj* s : g.rasterStates)
        if (memcmp(&s->desc, &d, sizeof(RasterDesc)) == 0) return s;
    RasterStateObj* s = new RasterStateObj();
    memset((void*)s, 0, sizeof(*s));
    s->desc = d;
    s->d3d.FillMode = d.wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    s->d3d.CullMode = (D3D12_CULL_MODE)d.cull;
    s->d3d.FrontCounterClockwise = d.frontCCW;
    s->d3d.DepthBias = d.depthBias;
    s->d3d.DepthBiasClamp = d.biasClamp;
    s->d3d.SlopeScaledDepthBias = d.slopeBias;
    s->d3d.DepthClipEnable = d.depthClip;
    s->d3d.MultisampleEnable = FALSE;
    s->d3d.AntialiasedLineEnable = FALSE;
    s->d3d.ForcedSampleCount = 0;
    s->d3d.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    s->id = (u32)g.rasterStates.size() + 1;
    g.rasterStates.push_back(s);
    return s;
}

DepthState createDepthState(const DepthDesc& d) {
    for (DepthStateObj* s : g.depthStates)
        if (memcmp(&s->desc, &d, sizeof(DepthDesc)) == 0) return s;
    DepthStateObj* s = new DepthStateObj();
    memset((void*)s, 0, sizeof(*s));
    s->desc = d;
    s->d3d.DepthEnable = d.test;
    s->d3d.DepthWriteMask = d.write ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    s->d3d.DepthFunc = (D3D12_COMPARISON_FUNC)d.func;
    s->d3d.StencilEnable = FALSE;
    s->d3d.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    s->d3d.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    D3D12_DEPTH_STENCILOP_DESC op = {D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_STENCIL_OP_KEEP, D3D12_COMPARISON_FUNC_ALWAYS};
    s->d3d.FrontFace = op;
    s->d3d.BackFace = op;
    s->id = (u32)g.depthStates.size() + 1;
    g.depthStates.push_back(s);
    return s;
}

InputLayout createInputLayout(const InputElement* e, int count) {
    if (!e || count <= 0) return nullptr;
    for (InputLayoutObj* l : g.inputLayouts) {
        if ((int)l->elems.size() != count) continue;
        bool same = true;
        for (int i = 0; i < count && same; i++) {
            const D3D12_INPUT_ELEMENT_DESC& a = l->elems[(size_t)i];
            same = l->names[(size_t)i] == e[i].semantic && a.SemanticIndex == e[i].semanticIndex && a.Format == e[i].format &&
                   a.InputSlot == e[i].slot && a.AlignedByteOffset == e[i].offset && (u32)a.InputSlotClass == (u32)e[i].cls &&
                   a.InstanceDataStepRate == e[i].stepRate;
        }
        if (same) return l;
    }
    InputLayoutObj* l = new InputLayoutObj();
    l->names.reserve((size_t)count);
    for (int i = 0; i < count; i++) l->names.push_back(e[i].semantic);
    for (int i = 0; i < count; i++) {
        D3D12_INPUT_ELEMENT_DESC d = {};
        d.SemanticName = l->names[(size_t)i].c_str();
        d.SemanticIndex = e[i].semanticIndex;
        d.Format = e[i].format;
        d.InputSlot = e[i].slot;
        d.AlignedByteOffset = e[i].offset;
        d.InputSlotClass = e[i].cls == PER_INSTANCE ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        d.InstanceDataStepRate = e[i].stepRate;
        l->elems.push_back(d);
    }
    l->id = (u32)g.inputLayouts.size() + 1;
    g.inputLayouts.push_back(l);
    return l;
}

// ---------------------------------------------------------------------------------------------------------------
// Resources (public)
void Texture::release() {
    if (res) releaseResource(res);
    res = nullptr;
    srv = uav = nullptr;
    rtv = nullptr;
    dsv = dsvRO = nullptr;
    mipUavs.clear();
    mipSrvs.clear();
    sliceRtvs.clear();
    sliceDsvs.clear();
    genSrvs.clear();
    genUavs.clear();
}

void Buffer::release() {
    if (buf) releaseResource(buf);
    buf = nullptr;
    srv = nullptr;
    uav = nullptr;
    size = 0;
}

void releaseResource(Resource r) {
    if (!r) return;
    if (ctx) ctx->onRelease(r);
    for (Context* c : g.asyncPool) c->onRelease(r);
    for (ViewObj* v : r->views) releaseView(v);
    r->views.clear();
    if (r->counter) releaseResource(r->counter);
    r->counter = nullptr;
    if (r->upload) {
        for (FrameData& f : g.frames) {
            auto it = std::find(f.written.begin(), f.written.end(), r);
            if (it != f.written.end()) f.written.erase(it);
        }
        if (r->liveSlot < g.liveUploads.size()) {
            ResourceObj* last = g.liveUploads.back();
            g.liveUploads[r->liveSlot] = last;
            last->liveSlot = r->liveSlot;
            g.liveUploads.pop_back();
        }
        r->liveSlot = ~(size_t)0;
        buryResource(r);
        return;
    }
    r->dead = true;
    Deferred d;
    d.obj = r->d3d;
    d.resource = r;
    retireTag(d.fence);
    d.minFrame = g.frame + kReleaseFrames;
    g.deferred.push_back(d);
}

int mipCount(int w, int h) {
    int m = 1;
    while (w > 1 || h > 1) { w = Max(1, w / 2); h = Max(1, h / 2); m++; }
    return m;
}

Texture createTexture(const TextureDesc& d, const void* initData, int initPitch) { return createTextureIn(d, initData, initPitch, nullptr, 0); }

Texture createTexture2D(int w, int h, DXGI_FORMAT fmt, u32 flags, int mips, int layers, const void* initData, int initPitch) {
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.format = fmt;
    d.flags = flags;
    d.mips = mips;
    d.layers = layers;
    return createTextureIn(d, initData, initPitch, nullptr, 0);
}

Texture createTexture3D(int w, int h, int dd, DXGI_FORMAT fmt, u32 flags, int mips) {
    TextureDesc d;
    d.width = w;
    d.height = h;
    d.depth = dd;
    d.is3D = true;
    d.format = fmt;
    d.flags = flags;
    d.mips = mips;
    return createTextureIn(d, nullptr, 0, nullptr, 0);
}

void uploadTexture2D(Texture& t, int mip, int layer, const void* data, int rowPitch) {
    if (!t.res) return;
    checkAlive(t.res, "uploadTexture2D");
    if (mip < 0 || mip >= t.mips || layer < 0 || layer >= t.layers)
        FatalError("Direct3D 12: uploadTexture2D mip %d layer %d outside the %s", mip, layer, describeResource(t.res).c_str());
    uploadSubresource(t.res, subresource((u32)mip, (u32)layer, (u32)t.mips), data, (u32)rowPitch);
}

Buffer createBuffer(u32 size, u32 stride, u32 flags, const void* init, const char* name) {
    return createBufferIn(size, stride, flags, init, name, nullptr, 0);
}

void updateBuffer(Buffer& b, const void* data, u32 size) {
    ResourceObj* r = b.buf;
    if (!r || size == 0) return;
    checkAlive(r, "updateBuffer");
    if (size > r->size) size = r->size;
    if (r->upload) {
        writeUpload(r, data, size);
        return;
    }
    if (r->readback) FatalError("Direct3D 12: updateBuffer on a readback buffer");
    UploadAlloc a = allocStaging(size, 16);
    memcpy(a.cpu, data, size);
    ctx->transition(r, D3D12_RESOURCE_STATE_COPY_DEST);
    ctx->flushBarriers();
    ctx->list()->CopyBufferRegion(r->d3d, 0, a.res, a.offset, size);
    ctx->anyWork = true;
}

const void* mapReadback(Buffer& b) {
    ResourceObj* r = b.buf;
    if (!r || !r->readback) return nullptr;
    return r->uploadCpu;   // readback buffers stay mapped for their lifetime
}

SRV createSRV(Resource r, const D3D12_SHADER_RESOURCE_VIEW_DESC& d) { return makeSRV(r, d); }
UAV createUAV(Resource r, const D3D12_UNORDERED_ACCESS_VIEW_DESC& d) { return makeUAV(r, d); }
RTV createRTV(Resource r, const D3D12_RENDER_TARGET_VIEW_DESC& d) { return makeRTV(r, d); }
DSV createDSV(Resource r, const D3D12_DEPTH_STENCIL_VIEW_DESC& d) { return makeDSV(r, d); }

// ---------------------------------------------------------------------------------------------------------------
// Heaps and placed resources
Heap createHeap(u64 size, HeapKind kind) {
    if (kind == HEAP_ANY && g.heapTier < D3D12_RESOURCE_HEAP_TIER_2)
        FatalError("Direct3D 12: mixed resource heaps need resource heap tier 2");
    HeapObj* h = new HeapObj();
    h->size = alignUp(size, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
    h->kind = kind;
    D3D12_HEAP_DESC d = {};
    d.SizeInBytes = h->size;
    d.Properties.Type = D3D12_HEAP_TYPE_DEFAULT;
    d.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    switch (kind) {
        case HEAP_RT_DS_TEXTURES: d.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES; break;
        case HEAP_TEXTURES: d.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES; break;
        case HEAP_BUFFERS: d.Flags = D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS; break;
        default: d.Flags = D3D12_HEAP_FLAG_ALLOW_ALL_BUFFERS_AND_TEXTURES; break;
    }
    checkHR(g.dev->CreateHeap(&d, __uuidof(ID3D12Heap), (void**)&h->d3d), "CreateHeap");
    return h;
}

void releaseHeap(Heap h) {
    if (!h) return;
    deferRelease(h->d3d);
    delete h;
}

AllocInfo textureAllocInfo(const TextureDesc& d) {
    TextureDesc t = d;
    if (t.depth > 1) t.is3D = true;
    int mips = t.mips <= 0 ? mipCount(t.width, t.height) : t.mips;
    D3D12_RESOURCE_DESC rd = textureResourceDesc(t, mips);
    D3D12_RESOURCE_ALLOCATION_INFO i = g.dev->GetResourceAllocationInfo(0, 1, &rd);
    AllocInfo a;
    a.size = i.SizeInBytes;
    a.alignment = i.Alignment;
    return a;
}

AllocInfo bufferAllocInfo(u32 size, u32 flags) {
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = (flags & BUF_CONSTANT) ? alignUp(size, 256) : size;
    d.Height = 1;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (flags & BUF_UAV) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    D3D12_RESOURCE_ALLOCATION_INFO i = g.dev->GetResourceAllocationInfo(0, 1, &d);
    AllocInfo a;
    a.size = i.SizeInBytes;
    a.alignment = i.Alignment;
    return a;
}

Texture createPlacedTexture(Heap h, u64 offset, const TextureDesc& d) {
    if (!h) FatalError("Direct3D 12: createPlacedTexture without a heap");
    return createTextureIn(d, nullptr, 0, h, offset);
}

Buffer createPlacedBuffer(Heap h, u64 offset, u32 size, u32 stride, u32 flags) {
    if (!h) FatalError("Direct3D 12: createPlacedBuffer without a heap");
    if (flags & (BUF_DYNAMIC | BUF_READBACK)) FatalError("Direct3D 12: placed buffers live in default heaps");
    return createBufferIn(size, stride, flags, nullptr, nullptr, h, offset);
}

CommandSignature createCommandSignature(IndirectKind kind, u32 rootConstants) {
    if (rootConstants > kRootConstants) FatalError("Direct3D 12: command signature with %u root constants", rootConstants);
    CommandSignatureObj* s = new CommandSignatureObj();
    s->kind = kind;
    s->rootConstants = rootConstants;
    D3D12_INDIRECT_ARGUMENT_TYPE t = kind == INDIRECT_DRAW ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW
                                                           : (kind == INDIRECT_DRAW_INDEXED ? D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED : D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH);
    s->d3d = makeCommandSignature(t, rootConstants, kind == INDIRECT_DISPATCH, s->stride);
    g.commandSignatures.push_back(s);
    return s;
}

// ---------------------------------------------------------------------------------------------------------------
// Device lifetime
const char* initError() { return g.initError.c_str(); }

bool init(void* hwnd, int width, int height, bool debugLayer) {
    g.mainThread = GetCurrentThreadId();
    if (!loadD3D12()) {
        g.initError = "d3d12.dll is not available (Direct3D 12 needs Windows 10 or newer)";
        LOG("Direct3D 12: %s", g.initError.c_str());
        return false;
    }
    if (debugLayer) {
        ID3D12Debug* dbg = nullptr;
        HRESULT hr = pD3D12GetDebugInterface ? pD3D12GetDebugInterface(__uuidof(ID3D12Debug), (void**)&dbg) : E_NOTIMPL;
        if (SUCCEEDED(hr) && dbg) {
            dbg->EnableDebugLayer();
            dbg->Release();
            g.debugLayer = true;
            LOG("D3D12 debug layer enabled");
        } else LOG("D3D12 debug layer requested but not available (%08lx): it needs the Graphics Tools optional feature (d3d12sdklayers.dll)", (unsigned long)hr);
    }
    IDXGIFactory1* f1 = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&f1)) || FAILED(f1->QueryInterface(__uuidof(IDXGIFactory4), (void**)&g.factory))) {
        SAFE_RELEASE(f1);
        g.initError = "DXGI 1.4 is not available (Windows 10 or newer is required)";
        return false;
    }
    SAFE_RELEASE(f1);
    // Adapter with the most dedicated video memory that can create a D3D12 device (the discrete GPU on laptops)
    IDXGIAdapter1* best = nullptr;
    SIZE_T bestMem = 0;
    for (UINT i = 0;; i++) {
        IDXGIAdapter1* a = nullptr;
        if (g.factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 d;
        a->GetDesc1(&d);
        bool ok = !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                  SUCCEEDED(pD3D12CreateDevice(a, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
        if (ok && (d.DedicatedVideoMemory > bestMem || !best)) {
            SAFE_RELEASE(best);
            best = a;
            bestMem = d.DedicatedVideoMemory;
        } else a->Release();
    }
    HRESULT hr = pD3D12CreateDevice(best, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&g.dev);
    if (FAILED(hr) && best) {
        SAFE_RELEASE(best);
        hr = pD3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&g.dev);
    }
    if (FAILED(hr) || !g.dev) {
        LOG("D3D12CreateDevice failed: %08lx", (unsigned long)hr);
        g.initError = StrFormat("no GPU with Direct3D 12 support was found (D3D12CreateDevice failed, %08lx)", (unsigned long)hr);
        SAFE_RELEASE(best);
        return false;
    }
    g.adapter = best;
    {
        LUID luid = g.dev->GetAdapterLuid();
        IDXGIAdapter1* a = nullptr;
        for (UINT i = 0; g.factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d;
            a->GetDesc1(&d);
            if (d.AdapterLuid.LowPart == luid.LowPart && d.AdapterLuid.HighPart == luid.HighPart) {
                char name[256];
                WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof(name), nullptr, nullptr);
                g.adapterName = name;
                g.vramMB = d.DedicatedVideoMemory / (1024 * 1024);
            }
            a->Release();
            a = nullptr;
        }
    }
    if (g.debugLayer && SUCCEEDED(g.dev->QueryInterface(__uuidof(ID3D12InfoQueue), (void**)&g.infoQueue))) {
        g.infoQueue->SetMuteDebugOutput(FALSE);
        LOG("D3D12 info queue attached (errors and warnings are logged)");
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS opt = {};
    if (SUCCEEDED(g.dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &opt, sizeof(opt)))) {
        g.bindingTier = opt.ResourceBindingTier;
        g.heapTier = opt.ResourceHeapTier;
        g.typedUavLoads = opt.TypedUAVLoadAdditionalFormats != 0;
    }
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D12_FEATURE_DATA_FEATURE_LEVELS fl = {4, levels, D3D_FEATURE_LEVEL_11_0};
    if (SUCCEEDED(g.dev->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &fl, sizeof(fl)))) g.featureLevel = fl.MaxSupportedFeatureLevel;
    if (g.bindingTier < D3D12_RESOURCE_BINDING_TIER_2) {
        g.initError = "the GPU only supports resource binding tier 1 (tier 2 or newer is required)";
        LOG("Direct3D 12: %s", g.initError.c_str());
        return false;
    }
    // queues and fences
    for (int q = 0; q < QUEUE_COUNT; q++) {
        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = q == QUEUE_DIRECT ? D3D12_COMMAND_LIST_TYPE_DIRECT : D3D12_COMMAND_LIST_TYPE_COMPUTE;
        checkHR(g.dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&g.queue[q]), "CreateCommandQueue");
        checkHR(g.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g.fence[q]), "CreateFence");
    }
    g.queue[QUEUE_DIRECT]->SetName(L"direct queue");
    g.queue[QUEUE_COMPUTE]->SetName(L"async compute queue");
    g.fenceEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    // swap chain
    IDXGIFactory5* f5 = nullptr;
    if (SUCCEEDED(g.factory->QueryInterface(__uuidof(IDXGIFactory5), (void**)&f5))) {
        BOOL allow = FALSE;
        if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow)))) g.allowTearing = allow != 0;
        f5->Release();
    }
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = (UINT)width;
    sd.Height = (UINT)height;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = DeviceState::kBackbuffers;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_STRETCH;
    sd.Flags = g.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    IDXGISwapChain1* sc1 = nullptr;
    hr = g.factory->CreateSwapChainForHwnd(g.queue[QUEUE_DIRECT], (HWND)hwnd, &sd, nullptr, nullptr, &sc1);
    if (FAILED(hr) || FAILED(sc1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&g.swap))) {
        LOG("CreateSwapChainForHwnd failed: %08lx", (unsigned long)hr);
        g.initError = StrFormat("the Direct3D 12 swap chain could not be created (%08lx)", (unsigned long)hr);
        SAFE_RELEASE(sc1);
        return false;
    }
    SAFE_RELEASE(sc1);
    g.factory->MakeWindowAssociation((HWND)hwnd, DXGI_MWA_NO_ALT_ENTER);
    g.fixedSyncInterval = wineBuiltinDxgi();
    if (g.fixedSyncInterval) LOG("DXGI is Wine's builtin implementation: presenting with sync interval 0 (no vsync)");
    g.bbW = width;
    g.bbH = height;
    // descriptor heaps
    g.descInc = g.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kGpuHeapSize, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    checkHR(g.dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&g.gpuHeap), "CreateDescriptorHeap (shader visible)");
    g.gpuHeapCpu = g.gpuHeap->GetCPUDescriptorHandleForHeapStart();
    g.gpuHeapGpu = g.gpuHeap->GetGPUDescriptorHandleForHeapStart();
    g.cpuSrv.type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    g.cpuSrv.inc = g.descInc;
    g.cpuRtv.type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    g.cpuRtv.inc = g.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    g.cpuDsv.type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    g.cpuDsv.inc = g.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    createSamplers();
    createRootSignatures();
    u32 stride = 0;
    g.drawSig = makeCommandSignature(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW, 0, false, stride);
    g.drawIndexedSig = makeCommandSignature(D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED, 0, false, stride);
    g.dispatchSig = makeCommandSignature(D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH, 0, true, stride);
    // the direct context records from here on (resource uploads during loading go into it)
    g.frames[g.frame % kFramesInFlight].number = g.frame;
    ctx = new Context();
    ctx->init(QUEUE_DIRECT);
    createBackbufferViews();
    createStates();
    std::vector<u8> zeros(65536, 0);
    g.zeroCB = createBuffer(65536, 0, BUF_CONSTANT, zeros.data(), "zero constants");
    LOG("Direct3D 12 device created: %s", featureSummary().c_str());
    if (!shaderCompilerInit()) {
        g.initError = "d3dcompiler_47.dll could not be loaded";
        return false;
    }
    g.mipGenCS = loadCS("mipgen.hlsl", "csMipGen");
    return true;
}

std::string featureSummary() {
    const char* fl = g.featureLevel >= D3D_FEATURE_LEVEL_12_1 ? "12_1"
                   : g.featureLevel >= D3D_FEATURE_LEVEL_12_0 ? "12_0"
                   : g.featureLevel >= D3D_FEATURE_LEVEL_11_1 ? "11_1" : "11_0";
    return StrFormat("adapter '%s', VRAM %zu MB, feature level %s, resource binding tier %d, resource heap tier %d, typed UAV loads %s, "
                     "tearing %s, bindless UAV arrays %s, %u frames in flight",
                     g.adapterName.c_str(), g.vramMB, fl, (int)g.bindingTier, (int)g.heapTier, g.typedUavLoads ? "yes" : "no",
                     g.allowTearing ? "yes" : "no", g.bindingTier >= D3D12_RESOURCE_BINDING_TIER_3 ? "yes" : "no", kFramesInFlight);
}

void waitIdle() {
    if (!g.dev) return;
    if (ctx && ctx->recording) ctx->submit();
    for (int q = 0; q < QUEUE_COUNT; q++) waitForFenceValue((QueueKind)q, g.fenceValue[q]);
    recycleCompleted(true);
    drainDebugMessages();
}

void shutdown() {
    if (!g.dev) {   // init failed before the device existed
        SAFE_RELEASE(g.adapter);
        SAFE_RELEASE(g.factory);
        return;
    }
    waitIdle();
    for (FrameData& f : g.frames) {
        for (UploadPage* p : f.pages) recyclePage(p);
        f.pages.clear();
        f.written.clear();
    }
    g.dynPage = nullptr;
    recycleCompleted(true);
    releaseBackbuffers();
    SAFE_RELEASE(g.swap);
    if (ctx) {
        SAFE_RELEASE(ctx->cl);
        delete ctx;
        ctx = nullptr;
    }
    for (Context* c : g.asyncPool) {
        SAFE_RELEASE(c->cl);
        delete c;
    }
    g.asyncPool.clear();
    for (auto& pool : g.allocators) {
        for (auto& e : pool) e.a->Release();
        pool.clear();
    }
    for (auto& kv : g.psoCache) kv.second->Release();
    g.psoCache.clear();
    for (UploadPage* p : g.freePages) destroyPage(p);
    for (UploadPage* p : g.freeDedicated) destroyPage(p);
    for (UploadPage* p : g.pagesInFlight) destroyPage(p);
    g.freePages.clear();
    g.freeDedicated.clear();
    g.pagesInFlight.clear();
    SAFE_RELEASE(g.drawSig);
    SAFE_RELEASE(g.drawIndexedSig);
    SAFE_RELEASE(g.dispatchSig);
    SAFE_RELEASE(g.rsGraphics);
    SAFE_RELEASE(g.rsCompute);
    SAFE_RELEASE(g.gpuHeap);
    SAFE_RELEASE(g.samplerHeap);
    g.cpuSrv.release();
    g.cpuRtv.release();
    g.cpuDsv.release();
    for (int q = 0; q < QUEUE_COUNT; q++) {
        SAFE_RELEASE(g.fence[q]);
        SAFE_RELEASE(g.queue[q]);
    }
    if (g.fenceEvent) CloseHandle(g.fenceEvent);
    g.fenceEvent = nullptr;
    SAFE_RELEASE(g.infoQueue);
    SAFE_RELEASE(g.adapter);
    SAFE_RELEASE(g.factory);
    SAFE_RELEASE(g.dev);
    for (ViewObj* v : g.deadViews) delete v;
    for (ResourceObj* r : g.deadResources) delete r;
    g.deadViews.clear();
    g.deadResources.clear();
}

void resize(int w, int h) {
    if (w <= 0 || h <= 0 || (w == g.bbW && h == g.bbH)) return;
    waitIdle();
    releaseBackbuffers();
    HRESULT hr = g.swap->ResizeBuffers(DeviceState::kBackbuffers, (UINT)w, (UINT)h, DXGI_FORMAT_UNKNOWN,
                                       g.allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0);
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) deviceLost("ResizeBuffers", hr);
        LOG("ResizeBuffers(%d x %d) failed %08lx", w, h, (unsigned long)hr);
    } else {
        g.bbW = w;
        g.bbH = h;
    }
    createBackbufferViews();
    LOG("Swap chain resized to %d x %d", g.bbW, g.bbH);
}

void timersFrameEnd(FrameData& f);
void timersFrameRetired(FrameData& f);

void present(bool vsync) {
    if (g.asyncOpen > 0) FatalError("Direct3D 12: an async compute context was still recording at present");
    FrameData& cur = curFrame();
    ResourceObj* bb = g.bb[g.swap->GetCurrentBackBufferIndex()];
    ctx->transition(bb, D3D12_RESOURCE_STATE_PRESENT);
    timersFrameEnd(cur);
    ctx->flushBarriers();
    u64 value = ctx->submit();
    int interval = vsync && !g.fixedSyncInterval ? 1 : 0;
    g.lastSyncInterval = interval;
    UINT flags = (!vsync && g.allowTearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    HRESULT hr = g.swap->Present((UINT)interval, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG) deviceLost("Present", hr);
    if (FAILED(hr)) LOG("Present failed: %08lx", (unsigned long)hr);
    cur.fence[QUEUE_DIRECT] = value;
    cur.fence[QUEUE_COMPUTE] = g.fenceValue[QUEUE_COMPUTE];
    // next frame: wait until the frame that used this slot before has finished on the GPU, then recycle its memory
    g.frame++;
    g.dynPage = nullptr;
    FrameData& next = curFrame();
    if (next.number) {
        for (int q = 0; q < QUEUE_COUNT; q++) waitForFenceValue((QueueKind)q, next.fence[q]);
        timersFrameRetired(next);
        retireFrame(next);
    }
    next.number = g.frame;
    next.fence[QUEUE_DIRECT] = next.fence[QUEUE_COMPUTE] = 0;
    recycleCompleted(false);
    drainDebugMessages();
}

RTV backbufferRTV() { return g.bbRtv[g.swap->GetCurrentBackBufferIndex()]; }
Resource backbuffer() { return g.bb[g.swap->GetCurrentBackBufferIndex()]; }
int backbufferWidth() { return g.bbW; }
int backbufferHeight() { return g.bbH; }
std::string adapterName() { return g.adapterName; }
size_t adapterVideoMemoryMB() { return g.vramMB; }
ID3D12Device* device() { return g.dev; }
ID3D12CommandQueue* commandQueue(QueueKind q) { return g.queue[q]; }
u64 frameNumber() { return g.frame; }
int d3d12ErrorCount() {
    drainDebugMessages();
    return g.debugErrors;
}

bool fenceReached(QueueKind q, u64 value) { return g.fence[q]->GetCompletedValue() >= value; }
void waitFence(QueueKind q, u64 value) { waitForFenceValue(q, value); }

Context* beginAsyncCompute() {
    Context* c = nullptr;
    for (Context* p : g.asyncPool)
        if (!p->recording) {
            c = p;
            break;
        }
    if (!c) {
        c = new Context();
        c->init(QUEUE_COMPUTE);
        g.asyncPool.push_back(c);
    } else c->beginList();
    g.asyncOpen++;
    return c;
}

u64 submitAsyncCompute(Context* c) {
    if (!c || c->queue() != QUEUE_COMPUTE || !c->recording) FatalError("Direct3D 12: submitAsyncCompute needs a recording async compute context");
    u64 v = c->submit();
    g.asyncOpen--;
    return v;
}

// ---------------------------------------------------------------------------------------------------------------
// Direct-context helpers
void setViewport(float w, float h, float x, float y) {
    Viewport vp;
    vp.x = x;
    vp.y = y;
    vp.w = w;
    vp.h = h;
    ctx->setViewport(vp);
}

void clearBindings() {
    SRV nullSrv[16] = {};
    UAV nullUav[8] = {};
    ctx->vsSetSRVs(0, 16, nullSrv);
    ctx->psSetSRVs(0, 16, nullSrv);
    ctx->csSetSRVs(0, 16, nullSrv);
    ctx->csSetUAVs(0, 8, nullUav);
    ctx->setRenderTargets(0, nullptr, nullptr);
}

void unbindCSResources(int srvCount, int uavCount) {
    SRV nullSrv[kMaxSRVSlots] = {};
    UAV nullUav[kMaxUAVSlots] = {};
    ctx->csSetSRVs(0, (u32)Min(srvCount, (int)kMaxSRVSlots), nullSrv);
    ctx->csSetUAVs(0, (u32)Min(uavCount, (int)kMaxUAVSlots), nullUav);
}

void beginEvent(const char* name) {
    if (ctx) ctx->beginEvent(name);
}
void endEvent() {
    if (ctx) ctx->endEvent();
}

}  // namespace gfx

#include "gfx_shaders.cpp"
#include "gfx_context.cpp"
#include "gfx_tools.cpp"
