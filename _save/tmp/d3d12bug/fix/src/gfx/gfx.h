// Direct3D 12 graphics layer.
//
// Core: device, a direct queue and an async compute queue with fences, a flip-model swap chain, one shader-visible
// CBV/SRV/UAV heap (a persistent bindless region plus a per-frame ring), upload pages, deferred destruction.
// Every SRV/UAV has a permanent bindless index into that heap; the default root signatures expose the whole heap
// as unbounded SM 5.1 arrays (see shaders/bindless.hlsli) next to 16 root constants.
//
// Convenience context (gfx::ctx): the passes bind resources in a slot model (t0..t47, b0..b3, u0..u7 per stage).
// Underneath, draws and dispatches resolve a pipeline state object from the bound state, copy the used slots into
// descriptor tables, set root CBVs, and transition resources automatically (per subresource, with UAV barriers
// between dependent dispatches). None of that caps the layer: explicit barriers feed the same tracker, and heaps
// with placed / aliased resources, ExecuteIndirect command signatures, the async compute queue with cross-queue
// waits, custom root signatures / pipelines and the raw device and command lists are all available.
#pragma once
#include "../core/base.h"
#include "../core/math.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while (0)

namespace gfx {

// ---------------------------------------------------------------------------------------------------------------
// Binding model of the default root signatures (register space0 unless noted)
constexpr u32 kMaxSRVSlots = 48;        // t0..t47: t0..t31 per pass, t32..t47 frame globals (two tables per stage)
constexpr u32 kLocalSRVSlots = 32;
constexpr u32 kMaxCBSlots = 4;          // b0..b3, root CBVs
constexpr u32 kMaxUAVSlots = 8;         // u0..u7, compute
constexpr u32 kRootConstants = 16;      // DWORDs at register(b0, space100), all stages
constexpr u32 kRootConstantSpace = 100;
constexpr u32 kBindlessSRVSpaces = 15;  // unbounded SRV arrays: register(t0, space1..15) index the whole heap
constexpr u32 kBindlessUAVSpaces = 8;   // unbounded UAV arrays: register(u0, space1..8) (resource binding tier 3)
constexpr u32 kMaxRenderTargets = 8;
constexpr u32 kMaxVertexBuffers = 4;
constexpr u32 kFramesInFlight = 2;
constexpr u32 kAllSubresources = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

enum Stage : u8 { STAGE_VS = 0, STAGE_PS = 1, STAGE_CS = 2, STAGE_COUNT = 3 };
enum QueueKind : u8 { QUEUE_DIRECT = 0, QUEUE_COMPUTE = 1, QUEUE_COUNT = 2 };

struct ViewObj;
struct HeapObj;

// ---------------------------------------------------------------------------------------------------------------
// Resources
enum ResourceKind : u8 { RES_BUFFER = 0, RES_TEXTURE2D, RES_TEXTURE3D };

struct ResourceObj {
    ID3D12Resource* d3d = nullptr;          // null for upload buffers (their memory lives in upload pages)
    ResourceKind kind = RES_BUFFER;
    D3D12_RESOURCE_DESC desc = {};
    u32 flags = 0;                          // TEX_* / BUF_* creation flags
    std::string name;
    // buffers
    u32 size = 0, stride = 0;
    bool upload = false;                    // BUF_DYNAMIC: CPU-written into upload pages, GPU-read, never transitioned
    bool readback = false;                  // BUF_READBACK: CPU-readable copy destination
    D3D12_GPU_VIRTUAL_ADDRESS gpuVA = 0;    // current GPU address (upload buffers: the latest write, 0 = never written)
    ID3D12Resource* uploadPage = nullptr;   // upload buffers: page holding the latest write
    u64 uploadOffset = 0;
    u32 uploadSize = 0;                     // bytes of the latest write
    u64 uploadFrame = 0;                    // frame of the latest write (contents are carried forward until rewritten)
    u8* uploadCpu = nullptr;                // CPU address of the latest write
    u32 version = 0;                        // bumps on every write of an upload buffer
    ResourceObj* counter = nullptr;         // hidden UAV counter of an append / consume buffer (BUF_APPEND)
    // textures
    u32 mips = 1, layers = 1, depth = 1;    // layers: array size (6 per cube)
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    // automatic state tracking
    u32 numSub = 1;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;    // all subresources while !perSub
    std::vector<D3D12_RESOURCE_STATES> subState;                   // per subresource while perSub
    bool perSub = false;
    bool uavPending = false;                // UAV writes not yet ordered by a UAV barrier
    bool promotedRead = false;              // buffers: current read state reached by implicit promotion
    u64 stateList = 0;                      // buffers: command list the tracked state belongs to (they decay after it)
    // placed resources
    HeapObj* heap = nullptr;
    u64 heapOffset = 0;
    std::vector<ViewObj*> views;            // released with the resource
    size_t liveSlot = ~(size_t)0;           // upload buffers: slot in the device's list of live upload buffers
    bool dead = false;                      // released: using it again is a fatal error (it stays in a quarantine)
};
typedef ResourceObj* Resource;

enum ViewKind : u8 { VIEW_SRV = 0, VIEW_UAV, VIEW_RTV, VIEW_DSV };
struct ViewObj {
    ViewKind kind = VIEW_SRV;
    ResourceObj* res = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = {};   // SRV/UAV: CPU staging descriptor (copy source for tables); RTV/DSV: heap handle
    u32 bindless = ~0u;                     // SRV/UAV: index in the shader-visible heap (bindless arrays)
    u32 mip0 = 0, mipCount = 1, slice0 = 0, sliceCount = 1;
    bool whole = true;                      // covers every subresource
    bool readOnly = false;                  // DSV with read-only depth
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    u32 cpuSlot = ~0u;                      // allocator bookkeeping
    bool dead = false;                      // released (kept in a quarantine so that later use is caught)
};
typedef ViewObj* SRV;
typedef ViewObj* UAV;
typedef ViewObj* RTV;
typedef ViewObj* DSV;

// Index of a view in the shader-visible heap, for the bindless arrays (~0u for none). Upload buffers get a fresh
// index on every write.
inline u32 bindlessIndex(const ViewObj* v) { return v ? v->bindless : ~0u; }

enum TexFlags : u32 {
    TEX_SRV = 1, TEX_RTV = 2, TEX_UAV = 4, TEX_DSV = 8, TEX_CUBE = 16, TEX_GENMIPS = 32, TEX_MIP_UAVS = 64,
    TEX_SLICE_RTVS = 128, TEX_DSV_READONLY = 256
};

struct TextureDesc {
    int width = 1, height = 1, depth = 1;   // depth > 1: 3D texture
    int mips = 1;                           // 0 = full chain
    int layers = 1;                         // array size (multiple of 6 for cubes)
    bool is3D = false;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;   // may be typeless when view formats are given
    DXGI_FORMAT srvFormat = DXGI_FORMAT_UNKNOWN, uavFormat = DXGI_FORMAT_UNKNOWN, rtvFormat = DXGI_FORMAT_UNKNOWN;
    u32 flags = 0;
    bool useClear = false;                  // optimized clear value for render targets / depth
    float clear[4] = {0, 0, 0, 0};          // color, or depth in [0]
    const char* name = nullptr;
};

struct Texture {
    Resource res = nullptr;
    SRV srv = nullptr;
    UAV uav = nullptr;
    RTV rtv = nullptr;
    DSV dsv = nullptr;
    DSV dsvRO = nullptr;                    // read-only depth (TEX_DSV_READONLY): depth test while sampling the depth
    std::vector<UAV> mipUavs;               // per-mip UAVs (TEX_MIP_UAVS)
    std::vector<SRV> mipSrvs;               // per-mip SRVs (TEX_MIP_UAVS)
    std::vector<RTV> sliceRtvs;             // per-array-slice RTVs (TEX_SLICE_RTVS)
    std::vector<DSV> sliceDsvs;             // per-array-slice DSVs (TEX_SLICE_RTVS + TEX_DSV)
    std::vector<SRV> genSrvs;               // TEX_GENMIPS: per-mip 2D-array views used by the mip generator
    std::vector<UAV> genUavs;
    int width = 0, height = 0, depth = 1, mips = 1, layers = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool is3D = false;
    bool srgb = false;                      // SRV format is sRGB (the mip generator re-encodes)
    void release();
};

Texture createTexture(const TextureDesc& d, const void* initData = nullptr, int initPitch = 0);
// 2D texture (optionally array / cube). Depth formats are passed typeless (R32_TYPELESS -> D32_FLOAT / R32_FLOAT views).
Texture createTexture2D(int w, int h, DXGI_FORMAT fmt, u32 flags, int mips = 1, int layers = 1, const void* initData = nullptr,
                        int initPitch = 0);
Texture createTexture3D(int w, int h, int d, DXGI_FORMAT fmt, u32 flags, int mips = 1);
// Uploads one subresource of a 2D texture from CPU memory (recorded on the direct context).
void uploadTexture2D(Texture& t, int mip, int layer, const void* data, int rowPitch);
int mipCount(int w, int h);
inline u32 subresource(u32 mip, u32 slice, u32 mips) { return mip + slice * mips; }

enum BufFlags : u32 {
    BUF_VERTEX = 1, BUF_INDEX = 2, BUF_CONSTANT = 4, BUF_STRUCTURED = 8, BUF_RAW = 16, BUF_UAV = 32,
    BUF_SRV = 64, BUF_DYNAMIC = 128, BUF_INDIRECT = 256, BUF_READBACK = 512, BUF_APPEND = 1024
};

struct Buffer {
    Resource buf = nullptr;
    SRV srv = nullptr;
    UAV uav = nullptr;
    u32 size = 0, stride = 0;
    u32 flags = 0;
    void release();
};

// BUF_DYNAMIC buffers are rewritten by updateBuffer (upload pages, like a map/discard: every write gets new memory
// and the contents persist until the next write). Other buffers live in default heaps (updateBuffer records a copy).
Buffer createBuffer(u32 size, u32 stride, u32 flags, const void* init = nullptr, const char* name = nullptr);
void updateBuffer(Buffer& b, const void* data, u32 size);
// BUF_READBACK buffers: CPU pointer to the contents (valid once the GPU finished the copies into them).
const void* mapReadback(Buffer& b);

template <typename T>
struct CBuffer {
    Buffer b;
    T data;
    void create() { b = createBuffer((sizeof(T) + 255) & ~255u, 0, BUF_CONSTANT | BUF_DYNAMIC); }
    void upload() { updateBuffer(b, &data, sizeof(T)); }
    Resource get() const { return b.buf; }
    void release() { b.release(); }
};

// Views on existing resources beyond the ones created with a texture / buffer (released with the resource).
SRV createSRV(Resource r, const D3D12_SHADER_RESOURCE_VIEW_DESC& d);
UAV createUAV(Resource r, const D3D12_UNORDERED_ACCESS_VIEW_DESC& d);
RTV createRTV(Resource r, const D3D12_RENDER_TARGET_VIEW_DESC& d);
DSV createDSV(Resource r, const D3D12_DEPTH_STENCIL_VIEW_DESC& d);
void releaseResource(Resource r);

// ---------------------------------------------------------------------------------------------------------------
// Heaps, placed resources and aliasing (transient render targets sharing memory)
enum HeapKind : u8 { HEAP_RT_DS_TEXTURES = 0, HEAP_TEXTURES, HEAP_BUFFERS, HEAP_ANY };
struct HeapObj {
    ID3D12Heap* d3d = nullptr;
    u64 size = 0;
    HeapKind kind = HEAP_ANY;
};
typedef HeapObj* Heap;
struct AllocInfo {
    u64 size = 0, alignment = 0;
};
Heap createHeap(u64 size, HeapKind kind);   // HEAP_ANY needs resource heap tier 2 (the other kinds work everywhere)
void releaseHeap(Heap h);
AllocInfo textureAllocInfo(const TextureDesc& d);
AllocInfo bufferAllocInfo(u32 size, u32 flags);
// Placed resources start with undefined contents: after an aliasing barrier, clear or discard render targets /
// depth before use (Context::discard).
Texture createPlacedTexture(Heap h, u64 offset, const TextureDesc& d);
Buffer createPlacedBuffer(Heap h, u64 offset, u32 size, u32 stride, u32 flags);

// ---------------------------------------------------------------------------------------------------------------
// Fixed-function state descriptions (baked into pipeline state objects)
enum Cull : u8 { CULL_NONE = 1, CULL_FRONT = 2, CULL_BACK = 3 };
enum Compare : u8 { CMP_NEVER = 1, CMP_LESS, CMP_EQUAL, CMP_LESS_EQUAL, CMP_GREATER, CMP_NOT_EQUAL, CMP_GREATER_EQUAL, CMP_ALWAYS };
enum Blend : u8 {
    BLEND_ZERO = 1, BLEND_ONE, BLEND_SRC_COLOR, BLEND_INV_SRC_COLOR, BLEND_SRC_ALPHA, BLEND_INV_SRC_ALPHA, BLEND_DEST_ALPHA,
    BLEND_INV_DEST_ALPHA, BLEND_DEST_COLOR, BLEND_INV_DEST_COLOR, BLEND_SRC_ALPHA_SAT
};
enum BlendOp : u8 { BLENDOP_ADD = 1, BLENDOP_SUBTRACT, BLENDOP_REV_SUBTRACT, BLENDOP_MIN, BLENDOP_MAX };
enum ColorWrite : u8 { WRITE_R = 1, WRITE_G = 2, WRITE_B = 4, WRITE_A = 8, WRITE_RGB = 7, WRITE_ALL = 15 };

struct RasterDesc {
    Cull cull = CULL_BACK;
    bool wireframe = false;
    bool frontCCW = true;     // counter-clockwise front faces (right-handed meshes)
    bool depthClip = true;
    bool scissor = false;     // false: the scissor rectangle covers the whole target
    int depthBias = 0;
    float slopeBias = 0.f, biasClamp = 0.f;
};
struct DepthDesc {
    bool test = true;
    bool write = true;
    Compare func = CMP_GREATER;   // reversed Z
};
struct RTBlend {
    bool enable = false;
    Blend src = BLEND_ONE, dst = BLEND_ZERO;
    BlendOp op = BLENDOP_ADD;
    Blend srcA = BLEND_ONE, dstA = BLEND_ZERO;
    BlendOp opA = BLENDOP_ADD;
    u8 writeMask = WRITE_ALL;
};
struct BlendDesc {
    bool independent = false;
    bool alphaToCoverage = false;
    RTBlend rt[8];
};

struct BlendStateObj { BlendDesc desc; D3D12_BLEND_DESC d3d; u32 id; };
struct RasterStateObj { RasterDesc desc; D3D12_RASTERIZER_DESC d3d; u32 id; };
struct DepthStateObj { DepthDesc desc; D3D12_DEPTH_STENCIL_DESC d3d; u32 id; };
typedef const BlendStateObj* BlendState;
typedef const RasterStateObj* RasterState;
typedef const DepthStateObj* DepthState;
// Identical descriptions return the same object (they are interned for the pipeline cache).
BlendState createBlendState(const BlendDesc& d);
RasterState createRasterState(const RasterDesc& d);
DepthState createDepthState(const DepthDesc& d);

struct States {
    RasterState cullBack = nullptr, cullFront = nullptr, cullNone = nullptr, wireframe = nullptr;
    RasterState shadowBias = nullptr;        // slope-scaled depth bias, cull none, no depth clip (pancaking)
    RasterState cullNoneScissor = nullptr;
    DepthState depthGreaterWrite = nullptr;  // reversed Z
    DepthState depthGreaterEqualNoWrite = nullptr;
    DepthState depthEqualNoWrite = nullptr;
    DepthState depthLessWrite = nullptr;     // shadow maps (standard Z)
    DepthState depthOff = nullptr;
    BlendState opaque = nullptr, alpha = nullptr, additive = nullptr, premultiplied = nullptr, noColorWrite = nullptr;
};
extern States states;
// The sampler set is fixed: s0..s6 of every default root signature come from a small shader-visible sampler heap
// (see shaders/common.hlsli): s0 point clamp, s1 linear clamp, s2 linear wrap, s3 anisotropic wrap, s4 shadow
// comparison (LESS_EQUAL, white border), s5 point wrap, s6 anisotropic clamp. (A sampler table rather than static
// samplers: vkd3d rejects root signatures that mix root descriptors with static samplers.)

// ---------------------------------------------------------------------------------------------------------------
// Shaders: HLSL embedded in the exe, compiled at runtime (SM 5.1 through d3dcompiler_47) and cached on disk.
struct ShaderDefine {
    const char* name;
    const char* value;
};

enum InputClass : u8 { PER_VERTEX = 0, PER_INSTANCE = 1 };
struct InputElement {
    const char* semantic;
    u32 semanticIndex;
    DXGI_FORMAT format;
    u32 slot;
    u32 offset;
    InputClass cls;
    u32 stepRate;
};
struct InputLayoutObj {
    std::vector<D3D12_INPUT_ELEMENT_DESC> elems;
    std::vector<std::string> names;
    u32 id = 0;
};
typedef const InputLayoutObj* InputLayout;
// Identical layouts return the same object.
InputLayout createInputLayout(const InputElement* e, int count);

// Resources a shader uses in the default binding model (from reflection).
struct ShaderBindings {
    u64 srvMask = 0;                 // t0..t47 (space0)
    u8 cbMask = 0;                   // b0..b3
    u8 uavMask = 0;                  // u0..u7
    bool rootConstants = false;      // reads register(b0, space100)
    bool bindless = false;           // uses the unbounded arrays
    u8 srvType[kMaxSRVSlots] = {};   // D3D_SHADER_INPUT_TYPE per used slot (null descriptors must match it)
    u8 srvDim[kMaxSRVSlots] = {};    // D3D_SRV_DIMENSION per used slot
    u16 srvStride[kMaxSRVSlots] = {};
    u8 uavType[kMaxUAVSlots] = {};
    u8 uavDim[kMaxUAVSlots] = {};
    u16 uavStride[kMaxUAVSlots] = {};
};
struct ShaderObj {
    Stage stage = STAGE_VS;
    std::vector<u8> code;
    ShaderBindings bind;
    u32 id = 0;
    std::string name;
    ID3D12PipelineState* computePSO = nullptr;   // compute shaders: PSO with the default compute root signature
};
typedef ShaderObj* PixelShader;
typedef ShaderObj* ComputeShader;
struct VertexShader {
    ShaderObj* vs = nullptr;
    InputLayout layout = nullptr;
};

bool shaderCompilerInit();
VertexShader loadVS(const char* file, const char* entry, const InputElement* layout, int layoutCount,
                    const std::vector<ShaderDefine>& defines = {});
PixelShader loadPS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines = {});
ComputeShader loadCS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines = {});
void releaseShader(ShaderObj* s);
int shaderCompileCount();
double shaderCompileSeconds();
int pipelineCount();         // graphics pipeline state objects created so far
double pipelineSeconds();    // CPU time spent creating them

// Custom root signatures (outside the default binding model); pipelines on them are created with the device.
ID3D12RootSignature* createRootSignature(const D3D12_ROOT_SIGNATURE_DESC& d);
ID3D12RootSignature* defaultGraphicsRootSignature();
ID3D12RootSignature* defaultComputeRootSignature();
// Root parameter indices of the default root signatures.
enum GraphicsRootParam : u32 {
    GRP_CB_VS = 0,       // + slot (b0..b3)
    GRP_CB_PS = 4,       // + slot
    GRP_SRV_VS_LOCAL = 8, GRP_SRV_VS_GLOBAL = 9, GRP_SRV_PS_LOCAL = 10, GRP_SRV_PS_GLOBAL = 11,
    GRP_ROOT_CONSTANTS = 12, GRP_BINDLESS = 13, GRP_SAMPLERS = 14, GRP_COUNT = 15
};
enum ComputeRootParam : u32 {
    CRP_CB = 0,          // + slot
    CRP_SRV_LOCAL = 4, CRP_SRV_GLOBAL = 5, CRP_UAV = 6, CRP_ROOT_CONSTANTS = 7, CRP_BINDLESS = 8, CRP_SAMPLERS = 9, CRP_COUNT = 10
};

// ---------------------------------------------------------------------------------------------------------------
// ExecuteIndirect
enum IndirectKind : u8 { INDIRECT_DRAW = 0, INDIRECT_DRAW_INDEXED, INDIRECT_DISPATCH };
struct CommandSignatureObj {
    ID3D12CommandSignature* d3d = nullptr;
    IndirectKind kind = INDIRECT_DRAW;
    u32 stride = 0;
    u32 rootConstants = 0;   // DWORDs of root constants (from offset 0) written per command before the draw / dispatch
};
typedef const CommandSignatureObj* CommandSignature;
// Arguments per command: [rootConstants DWORDs] + D3D12_DRAW_ARGUMENTS / D3D12_DRAW_INDEXED_ARGUMENTS /
// D3D12_DISPATCH_ARGUMENTS, tightly packed.
CommandSignature createCommandSignature(IndirectKind kind, u32 rootConstants = 0);

// ---------------------------------------------------------------------------------------------------------------
enum Topology : u8 { TOPO_TRIANGLE_LIST = 0, TOPO_TRIANGLE_STRIP, TOPO_LINE_LIST, TOPO_LINE_STRIP, TOPO_POINT_LIST };

struct Viewport {
    float x = 0, y = 0, w = 0, h = 0, minZ = 0, maxZ = 1;
};

// Command context. The direct context (gfx::ctx) lives for the whole run and submits at present (or earlier, see
// submit / waitIdle); async compute contexts come from beginAsyncCompute().
class Context {
public:
    // ---- pipeline
    void setVS(ShaderObj* vs);
    void setPS(PixelShader ps);
    void setCS(ComputeShader cs);
    void setInputLayout(InputLayout il);
    void setTopology(Topology t);
    void setBlendState(BlendState s);
    void setDepthState(DepthState s);
    void setRasterState(RasterState s);
    // ---- resources (slot model)
    void vsSetSRVs(u32 slot, u32 n, const SRV* v) { setSRVs(STAGE_VS, slot, n, v); }
    void psSetSRVs(u32 slot, u32 n, const SRV* v) { setSRVs(STAGE_PS, slot, n, v); }
    void csSetSRVs(u32 slot, u32 n, const SRV* v) { setSRVs(STAGE_CS, slot, n, v); }
    void setSRVs(Stage s, u32 slot, u32 n, const SRV* v);
    void vsSetCBs(u32 slot, u32 n, const Resource* b) { setCBs(STAGE_VS, slot, n, b); }
    void psSetCBs(u32 slot, u32 n, const Resource* b) { setCBs(STAGE_PS, slot, n, b); }
    void csSetCBs(u32 slot, u32 n, const Resource* b) { setCBs(STAGE_CS, slot, n, b); }
    void setCBs(Stage s, u32 slot, u32 n, const Resource* b);
    // initialCounts (optional, ~0u = keep): resets append / consume counters before the next dispatch
    void csSetUAVs(u32 slot, u32 n, const UAV* v, const u32* initialCounts = nullptr);
    // Root constants at register(b0, space100): graphics (all stages) or compute
    void setRootConstants(bool compute, u32 offset, u32 count, const void* data);
    void setVertexBuffers(u32 slot, u32 n, const Resource* b, const u32* strides, const u32* offsets);
    void setIndexBuffer(Resource b, DXGI_FORMAT fmt, u32 offset = 0);
    void setRenderTargets(u32 n, const RTV* rtv, DSV dsv);
    void getRenderTargets(RTV* rtv0, DSV* dsv) const;
    void setViewport(const Viewport& vp);
    Viewport getViewport() const { return viewport; }
    void setScissor(int x, int y, int w, int h);   // used by raster states with the scissor test enabled
    // ---- work
    void draw(u32 vertexCount, u32 startVertex);
    void drawIndexed(u32 indexCount, u32 startIndex, int baseVertex);
    void drawInstanced(u32 vertsPerInstance, u32 instances, u32 startVertex, u32 startInstance);
    void drawIndexedInstanced(u32 indicesPerInstance, u32 instances, u32 startIndex, int baseVertex, u32 startInstance);
    void drawInstancedIndirect(Resource args, u32 offset);          // D3D12_DRAW_ARGUMENTS at offset
    void dispatch(u32 x, u32 y, u32 z);
    void dispatchIndirect(Resource args, u32 offset);               // D3D12_DISPATCH_ARGUMENTS at offset
    // Up to maxCount commands (or the count in countBuffer when given) with the bound pipeline and resources.
    void executeIndirect(CommandSignature sig, u32 maxCount, Resource args, u64 argOffset, Resource count = nullptr,
                         u64 countOffset = 0);
    // ---- clears and copies
    void clearRTV(RTV v, const float color[4]);
    void clearDepth(DSV v, float depth);
    void clearUAVFloat(UAV v, const float values[4]);
    void clearUAVUint(UAV v, const u32 values[4]);
    void copyResource(Resource dst, Resource src);
    void copySubresource(Resource dst, u32 dstSub, Resource src, u32 srcSub);
    void copyBufferRegion(Resource dst, u64 dstOffset, Resource src, u64 srcOffset, u64 size);
    void copyStructureCount(Resource dst, u32 dstOffset, UAV src);   // append / consume counter -> buffer
    void generateMips(const Texture& t);                               // TEX_GENMIPS textures (compute downsampler)
    // ---- explicit barriers (they update the automatic tracker, so both styles mix)
    void transition(Resource r, D3D12_RESOURCE_STATES state, u32 subresource = kAllSubresources);
    void uavBarrier(Resource r);        // null: all UAV accesses
    void aliasingBarrier(Resource before, Resource after);
    void discard(Resource r);           // placed render targets / depth after an aliasing barrier
    void flushBarriers();
    // ---- queue synchronization
    u64 submit();                       // executes the recorded work, returns the fence value it signals
    void wait(QueueKind q, u64 value);  // later work of this context waits (on the GPU) for that queue's fence value
    ID3D12GraphicsCommandList* list() { return cl; }
    QueueKind queue() const { return queueKind; }
    // ---- debug markers
    void beginEvent(const char* name);
    void endEvent();

    // ---- internals (used by the gfx implementation)
    void init(QueueKind q);
    void beginList();
    void invalidateApplied();
    void onRelease(ResourceObj* r);
    void onViewRelease(ViewObj* v);
    void onShaderRelease(ShaderObj* s);
    void markSRVTablesDirty();
    ID3D12GraphicsCommandList* cl = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    QueueKind queueKind = QUEUE_DIRECT;
    u64 listId = 0;
    bool recording = false;
    bool anyWork = false;

private:
    // bound state
    ShaderObj* shaders[STAGE_COUNT] = {};
    InputLayout inputLayout = nullptr;
    Topology topology = TOPO_TRIANGLE_LIST;
    BlendState blend = nullptr;
    DepthState depthState = nullptr;
    RasterState raster = nullptr;
    SRV srvs[STAGE_COUNT][kMaxSRVSlots] = {};
    Resource cbs[STAGE_COUNT][kMaxCBSlots] = {};
    UAV uavs[kMaxUAVSlots] = {};
    u32 uavInitCount[kMaxUAVSlots] = {};
    u8 uavInitMask = 0;              // append / consume counters to reset before the next dispatch
    u32 rootConst[2][kRootConstants] = {};
    bool rootConstDirty[2] = {true, true};
    Resource vbs[kMaxVertexBuffers] = {};
    u32 vbStride[kMaxVertexBuffers] = {}, vbOffset[kMaxVertexBuffers] = {};
    Resource ib = nullptr;
    DXGI_FORMAT ibFormat = DXGI_FORMAT_R32_UINT;
    u32 ibOffset = 0;
    RTV rtvs[kMaxRenderTargets] = {};
    u32 numRTV = 0;
    DSV dsvBound = nullptr;
    Viewport viewport;
    D3D12_RECT scissor = {0, 0, 16384, 16384};
    // applied state (what the command list holds)
    ID3D12PipelineState* graphicsPSO = nullptr;   // resolved from the bound state
    ID3D12PipelineState* curPSO = nullptr;
    ID3D12RootSignature* curGraphicsRS = nullptr;
    ID3D12RootSignature* curComputeRS = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS appliedCB[STAGE_COUNT][kMaxCBSlots] = {};
    D3D12_VERTEX_BUFFER_VIEW appliedVB[kMaxVertexBuffers] = {};
    u32 appliedVBCount = 0;
    D3D12_INDEX_BUFFER_VIEW appliedIB = {};
    bool srvTableDirty[STAGE_COUNT][2] = {};
    bool uavTableDirty = true;
    bool psoDirty = true, rtDirty = true, vpDirty = true, scissorDirty = true, topoDirty = true;
    bool statesDirty[2] = {true, true};   // graphics / compute bindings that need resource transitions changed
    bool iaStatesDirty = true;            // vertex / index buffers changed
    u64 appliedEpoch[2] = {};             // device state epoch after the last transition pass (graphics / compute)
    bool bindlessApplied[2] = {};
    struct Req {
        ResourceObj* r;
        u32 mip0, mipCount, slice0, sliceCount;
        D3D12_RESOURCE_STATES s;
    };
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    std::vector<Req> reqs;
    std::vector<D3D12_RESOURCE_STATES> tmpStates;
    int eventDepth = 0;
    void checkGraphics(const char* what) const;
    void require(ResourceObj* r, D3D12_RESOURCE_STATES s);
    void requireView(ViewObj* v, D3D12_RESOURCE_STATES s);
    void resolveRequirements();
    void transitionWhole(ResourceObj* r, D3D12_RESOURCE_STATES s);
    void transitionSub(ResourceObj* r, u32 sub, D3D12_RESOURCE_STATES s);
    void pushTransition(ResourceObj* r, u32 sub, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after);
    bool srvBlockedByOutput(ViewObj* v, bool compute) const;
    void prepareDraw();
    void prepareDispatch();
    void buildSRVTable(Stage st, u32 range, u32 rootParam, bool compute);
    void buildUAVTable();
    void applyRootCBVs(Stage st, bool compute);
    void applyPendingCounters();
    ID3D12PipelineState* resolveGraphicsPSO();
    void markUAVWrites();
    void afterUAVClear();
    D3D12_RESOURCE_STATES srvState() const;
};

extern Context* ctx;   // the direct (graphics) context

// Async compute: a context on the compute queue. Record, then submit it; the returned fence value can be waited on
// by the other queue (ctx->wait(QUEUE_COMPUTE, v)). Resources it touches must be in compute-legal states (prepare
// them on the direct context with transition()); SRV reads use NON_PIXEL_SHADER_RESOURCE there.
Context* beginAsyncCompute();
u64 submitAsyncCompute(Context* c);
bool fenceReached(QueueKind q, u64 value);
void waitFence(QueueKind q, u64 value);   // CPU wait

// ---------------------------------------------------------------------------------------------------------------
// Device
bool init(void* hwnd, int width, int height, bool debugLayer);
const char* initError();         // why init() failed (for the error message box)
void shutdown();
void resize(int width, int height);
void present(bool vsync);
void waitIdle();                 // submits the recorded work and waits for every queue to finish
RTV backbufferRTV();
Resource backbuffer();
int backbufferWidth();
int backbufferHeight();
std::string adapterName();
size_t adapterVideoMemoryMB();
std::string featureSummary();    // device capabilities (logged at startup)
ID3D12Device* device();
ID3D12CommandQueue* commandQueue(QueueKind q);
u64 frameNumber();
int d3d12ErrorCount();           // errors reported by the D3D12 debug layer (--d3ddebug) so far

// Helpers on the direct context
void setViewport(float w, float h, float x = 0, float y = 0);
void clearBindings();            // unbind SRVs / UAVs / render targets in every stage
void unbindCSResources(int srvCount = 16, int uavCount = 8);
inline u32 divUp(u32 a, u32 b) { return (a + b - 1) / b; }

// GPU timing (optional overlay, --gputimers)
void gpuTimerBegin(const char* name);
void gpuTimerEnd();
void gpuTimersResolve();
std::string gpuTimerReport();

// Screenshot: copies the back buffer to the CPU and writes a BMP file.
bool saveScreenshotBMP(const char* path);
// Debug readbacks (they wait for the GPU)
bool readbackPixelsFloat4(Resource tex, DXGI_FORMAT fmt, int x, int y, float out[4]);
bool readbackBuffer(Resource buf, void* out, u32 size);

// Debug markers (PIX / RenderDoc captures)
void beginEvent(const char* name);
void endEvent();

// Exercises the D3D12 features the layer exposes (bindless arrays, root constants, async compute with cross-queue
// waits, ExecuteIndirect with a count buffer, placed aliased render targets, append counters, mip generation,
// per-subresource transitions) and checks the results on the CPU. Returns the number of failures (0 = all passed);
// details go to the log.
int selfTest();

}  // namespace gfx
