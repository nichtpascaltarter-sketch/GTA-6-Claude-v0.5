// Thin Direct3D 11 wrapper: device, swap chain, resources, shaders, common states.
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
#include <d3d11.h>
#include <dxgi1_2.h>

#define SAFE_RELEASE(p) do { if (p) { (p)->Release(); (p) = nullptr; } } while (0)

namespace gfx {

extern ID3D11Device* dev;
extern ID3D11DeviceContext* ctx;

bool init(void* hwnd, int width, int height, bool debugLayer);
void shutdown();
void resize(int width, int height);
void present(bool vsync);
ID3D11RenderTargetView* backbufferRTV();
ID3D11Texture2D* backbufferTex();
int backbufferWidth();
int backbufferHeight();
std::string adapterName();
size_t adapterVideoMemoryMB();

// ---------------------------------------------------------------------------
enum TexFlags : u32 {
    TEX_SRV = 1, TEX_RTV = 2, TEX_UAV = 4, TEX_DSV = 8, TEX_CUBE = 16, TEX_GENMIPS = 32, TEX_MIP_UAVS = 64,
    TEX_SLICE_RTVS = 128, TEX_DYNAMIC = 256
};

struct Texture {
    ID3D11Resource* res = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11UnorderedAccessView* uav = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    std::vector<ID3D11UnorderedAccessView*> mipUavs;   // per-mip UAVs (TEX_MIP_UAVS)
    std::vector<ID3D11ShaderResourceView*> mipSrvs;    // per-mip SRVs (TEX_MIP_UAVS)
    std::vector<ID3D11RenderTargetView*> sliceRtvs;    // per-array-slice RTVs (TEX_SLICE_RTVS)
    std::vector<ID3D11DepthStencilView*> sliceDsvs;    // per-array-slice DSVs (TEX_SLICE_RTVS + TEX_DSV)
    int width = 0, height = 0, depth = 1, mips = 1, layers = 1;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    bool is3D = false;
    ID3D11Texture2D* tex2d() const { return (ID3D11Texture2D*)res; }
    void release();
};

// Creates a 2D texture (optionally array/cube). For depth formats pass the typeless
// format; views are derived automatically (R32_TYPELESS -> D32_FLOAT / R32_FLOAT).
Texture createTexture2D(int w, int h, DXGI_FORMAT fmt, u32 flags, int mips = 1, int layers = 1,
                        const void* initData = nullptr, int initPitch = 0);
Texture createTexture3D(int w, int h, int d, DXGI_FORMAT fmt, u32 flags, int mips = 1);
// Upload one mip/slice of a 2D texture from CPU memory (default usage textures).
void uploadTexture2D(Texture& t, int mip, int layer, const void* data, int rowPitch);
int mipCount(int w, int h);

enum BufFlags : u32 {
    BUF_VERTEX = 1, BUF_INDEX = 2, BUF_CONSTANT = 4, BUF_STRUCTURED = 8, BUF_RAW = 16, BUF_UAV = 32,
    BUF_SRV = 64, BUF_DYNAMIC = 128, BUF_INDIRECT = 256, BUF_STAGING = 512, BUF_APPEND = 1024
};

struct Buffer {
    ID3D11Buffer* buf = nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    ID3D11UnorderedAccessView* uav = nullptr;
    u32 size = 0, stride = 0;
    u32 flags = 0;
    void release();
};

Buffer createBuffer(u32 size, u32 stride, u32 flags, const void* init = nullptr);
// Updates a dynamic buffer with map/discard, or a default buffer with UpdateSubresource.
void updateBuffer(Buffer& b, const void* data, u32 size);

// Typed dynamic constant buffer.
template <typename T>
struct CBuffer {
    Buffer b;
    T data;
    void create() { b = createBuffer((sizeof(T) + 15) & ~15u, 0, BUF_CONSTANT | BUF_DYNAMIC); }
    void upload() { updateBuffer(b, &data, sizeof(T)); }
    ID3D11Buffer* get() const { return b.buf; }
    void release() { b.release(); }
};

// ---------------------------------------------------------------------------
// Shaders are compiled at runtime from embedded HLSL through d3dcompiler_47.dll
// (ships with Windows 10/11) and cached on disk by source hash.
struct ShaderDefine {
    const char* name;
    const char* value;
};

struct VertexShader {
    ID3D11VertexShader* vs = nullptr;
    ID3D11InputLayout* layout = nullptr;
};

bool shaderCompilerInit();
// Compile & create. Returns nullptr on failure after logging errors (fatal in dev builds).
VertexShader loadVS(const char* file, const char* entry, const D3D11_INPUT_ELEMENT_DESC* layout, int layoutCount,
                    const std::vector<ShaderDefine>& defines = {});
ID3D11PixelShader* loadPS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines = {});
ID3D11ComputeShader* loadCS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines = {});
ID3D11GeometryShader* loadGS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines = {});
// Queue for parallel compilation: call precompile for all shaders first (optional).
int shaderCompileCount();
double shaderCompileSeconds();

// ---------------------------------------------------------------------------
// Common fixed-function states.
struct States {
    ID3D11RasterizerState* cullBack = nullptr;
    ID3D11RasterizerState* cullFront = nullptr;
    ID3D11RasterizerState* cullNone = nullptr;
    ID3D11RasterizerState* wireframe = nullptr;
    ID3D11RasterizerState* shadowBias = nullptr;       // depth bias, cull none
    ID3D11RasterizerState* cullNoneScissor = nullptr;
    ID3D11DepthStencilState* depthGreaterWrite = nullptr;  // reversed Z
    ID3D11DepthStencilState* depthGreaterEqualNoWrite = nullptr;
    ID3D11DepthStencilState* depthEqualNoWrite = nullptr;
    ID3D11DepthStencilState* depthLessWrite = nullptr;     // shadow maps (standard Z)
    ID3D11DepthStencilState* depthOff = nullptr;
    ID3D11BlendState* opaque = nullptr;
    ID3D11BlendState* alpha = nullptr;
    ID3D11BlendState* additive = nullptr;
    ID3D11BlendState* premultiplied = nullptr;
    ID3D11BlendState* noColorWrite = nullptr;
    ID3D11SamplerState* pointClamp = nullptr;
    ID3D11SamplerState* linearClamp = nullptr;
    ID3D11SamplerState* linearWrap = nullptr;
    ID3D11SamplerState* anisoWrap = nullptr;
    ID3D11SamplerState* shadowCmp = nullptr;  // comparison sampler (LESS_EQUAL)
    ID3D11SamplerState* pointWrap = nullptr;
    ID3D11SamplerState* anisoClamp = nullptr;
};
extern States states;

// Helpers
void setViewport(float w, float h, float x = 0, float y = 0);
void clearBindings();  // unbind SRVs/UAVs/RTVs to avoid hazards between passes
void unbindCSResources(int srvCount = 16, int uavCount = 8);
inline u32 divUp(u32 a, u32 b) { return (a + b - 1) / b; }

// GPU timing (optional overlay)
void gpuTimerBegin(const char* name);
void gpuTimerEnd();
void gpuTimersResolve();
std::string gpuTimerReport();

// Screenshot: copies back buffer to CPU and writes a BMP file.
bool saveScreenshotBMP(const char* path);

// Debug: read back a small region of a texture (R16G16B16A16_FLOAT or R32 formats) as floats.
bool readbackPixelsFloat4(ID3D11Resource* res, DXGI_FORMAT fmt, int x, int y, float out[4]);
bool readbackBuffer(ID3D11Buffer* buf, void* out, u32 size);

// Debug annotation markers (no-op when unavailable)
void beginEvent(const char* name);
void endEvent();

}  // namespace gfx
