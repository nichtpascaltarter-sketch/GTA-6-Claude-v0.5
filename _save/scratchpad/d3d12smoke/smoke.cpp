// D3D12 smoke test for the Wine (vkd3d + lavapipe) test rig: device, queue, swap chain, root signature, a compute
// dispatch writing a UAV buffer (SM 5.1 via d3dcompiler_47), a render-target clear, readback of both, present.
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>
#include <stdio.h>

#define CHECK(x)                                                                    \
    do {                                                                            \
        HRESULT hr_ = (x);                                                          \
        if (FAILED(hr_)) {                                                          \
            printf("FAIL %s -> 0x%08lx\n", #x, (unsigned long)hr_);                 \
            fflush(stdout);                                                         \
            return 1;                                                               \
        }                                                                           \
    } while (0)

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

typedef HRESULT(WINAPI* PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                     ID3DBlob**, ID3DBlob**);

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSA wc = {};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "d3d12smoke";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("d3d12smoke", "d3d12smoke", WS_OVERLAPPEDWINDOW, 0, 0, 256, 256, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    IDXGIFactory4* factory = nullptr;
    CHECK(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory));
    IDXGIAdapter1* adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; i++) {
        DXGI_ADAPTER_DESC1 d;
        adapter->GetDesc1(&d);
        printf("adapter %u: %ls vram %llu MB flags %u\n", i, d.Description, (unsigned long long)(d.DedicatedVideoMemory >> 20), d.Flags);
        adapter->Release();
        adapter = nullptr;
    }
    ID3D12Device* dev = nullptr;
    CHECK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&dev));
    D3D12_FEATURE_DATA_D3D12_OPTIONS opt = {};
    dev->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &opt, sizeof(opt));
    printf("device ok: resource binding tier %d, tiled tier %d, ROV %d, conservative raster tier %d\n", (int)opt.ResourceBindingTier,
           (int)opt.TiledResourcesTier, (int)opt.ROVsSupported, (int)opt.ConservativeRasterizationTier);
    D3D12_FEATURE_DATA_SHADER_MODEL sm = {D3D_SHADER_MODEL_6_0};
    if (SUCCEEDED(dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm)))) printf("highest shader model 0x%x\n", (int)sm.HighestShaderModel);

    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    CHECK(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue));

    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = 256;
    sd.Height = 256;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.SampleDesc.Count = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = 2;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1* sc1 = nullptr;
    CHECK(factory->CreateSwapChainForHwnd(queue, hwnd, &sd, nullptr, nullptr, &sc1));
    IDXGISwapChain3* sc = nullptr;
    CHECK(sc1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&sc));
    printf("swap chain ok\n");

    // compute shader through d3dcompiler_47 (SM 5.1)
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    if (!dc) { printf("FAIL no d3dcompiler_47\n"); return 1; }
    PFN_Compile compile = (PFN_Compile)GetProcAddress(dc, "D3DCompile");
    const char* src =
        "RWStructuredBuffer<uint> outBuf : register(u0);\n"
        "cbuffer C : register(b0) { uint mul; };\n"
        "[numthreads(64,1,1)] void main(uint3 id : SV_DispatchThreadID) { outBuf[id.x] = id.x * mul + 7; }\n";
    ID3DBlob *cs = nullptr, *err = nullptr;
    if (FAILED(compile(src, strlen(src), "smoke", nullptr, nullptr, "main", "cs_5_1", 0, 0, &cs, &err))) {
        printf("FAIL compile: %s\n", err ? (const char*)err->GetBufferPointer() : "?");
        return 1;
    }
    printf("cs_5_1 compiled: %u bytes\n", (unsigned)cs->GetBufferSize());

    // root signature: 1 root constant (b0), 1 UAV descriptor table (u0)
    D3D12_DESCRIPTOR_RANGE range = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND};
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = 1;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    D3D12_ROOT_SIGNATURE_DESC rsd = {2, params, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob* rsBlob = nullptr;
    CHECK(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &err));
    ID3D12RootSignature* rs = nullptr;
    CHECK(dev->CreateRootSignature(0, rsBlob->GetBufferPointer(), rsBlob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&rs));
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = rs;
    pd.CS = {cs->GetBufferPointer(), cs->GetBufferSize()};
    ID3D12PipelineState* pso = nullptr;
    CHECK(dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&pso));
    printf("compute PSO ok\n");

    const UINT N = 256;
    D3D12_HEAP_PROPERTIES hpDefault = {D3D12_HEAP_TYPE_DEFAULT}, hpRead = {D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd = {};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = N * 4;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource *buf = nullptr, *rb = nullptr, *rbTex = nullptr;
    CHECK(dev->CreateCommittedResource(&hpDefault, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, __uuidof(ID3D12Resource),
                                       (void**)&buf));
    bd.Flags = D3D12_RESOURCE_FLAG_NONE;
    CHECK(dev->CreateCommittedResource(&hpRead, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&rb));
    bd.Width = 256 * 256 * 4;
    CHECK(dev->CreateCommittedResource(&hpRead, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                       (void**)&rbTex));

    D3D12_DESCRIPTOR_HEAP_DESC hd = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ID3D12DescriptorHeap* heap = nullptr;
    CHECK(dev->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&heap));
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = DXGI_FORMAT_UNKNOWN;
    ud.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = N;
    ud.Buffer.StructureByteStride = 4;
    dev->CreateUnorderedAccessView(buf, nullptr, &ud, heap->GetCPUDescriptorHandleForHeapStart());

    D3D12_DESCRIPTOR_HEAP_DESC rh = {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
    ID3D12DescriptorHeap* rtvHeap = nullptr;
    CHECK(dev->CreateDescriptorHeap(&rh, __uuidof(ID3D12DescriptorHeap), (void**)&rtvHeap));
    UINT rtvInc = dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12Resource* back[2] = {};
    for (UINT i = 0; i < 2; i++) {
        CHECK(sc->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&back[i]));
        D3D12_CPU_DESCRIPTOR_HANDLE h = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += i * rtvInc;
        dev->CreateRenderTargetView(back[i], nullptr, h);
    }

    ID3D12CommandAllocator* alloc = nullptr;
    CHECK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&alloc));
    ID3D12GraphicsCommandList* cl = nullptr;
    CHECK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, pso, __uuidof(ID3D12GraphicsCommandList), (void**)&cl));
    ID3D12Fence* fence = nullptr;
    CHECK(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence));
    HANDLE ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);

    // compute
    cl->SetComputeRootSignature(rs);
    cl->SetDescriptorHeaps(1, &heap);
    cl->SetComputeRoot32BitConstant(0, 3, 0);
    cl->SetComputeRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
    cl->Dispatch(N / 64, 1, 1);
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = buf;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl->ResourceBarrier(1, &b);
    cl->CopyResource(rb, buf);

    // clear the back buffer and read it back
    UINT bi = sc->GetCurrentBackBufferIndex();
    b.Transition.pResource = back[bi];
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    cl->ResourceBarrier(1, &b);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += bi * rtvInc;
    const float color[4] = {0.25f, 0.5f, 0.75f, 1.f};
    cl->ClearRenderTargetView(rtv, color, 0, nullptr);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    cl->ResourceBarrier(1, &b);
    D3D12_TEXTURE_COPY_LOCATION dst = {}, srcLoc = {};
    dst.pResource = rbTex;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, 256, 256, 1, 256 * 4};
    srcLoc.pResource = back[bi];
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    cl->CopyTextureRegion(&dst, 0, 0, 0, &srcLoc, nullptr);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    cl->ResourceBarrier(1, &b);
    CHECK(cl->Close());
    ID3D12CommandList* lists[] = {cl};
    queue->ExecuteCommandLists(1, lists);
    CHECK(queue->Signal(fence, 1));
    CHECK(fence->SetEventOnCompletion(1, ev));
    WaitForSingleObject(ev, 10000);
    printf("gpu done (fence %llu)\n", (unsigned long long)fence->GetCompletedValue());

    unsigned* p = nullptr;
    D3D12_RANGE r = {0, N * 4};
    CHECK(rb->Map(0, &r, (void**)&p));
    int bad = 0;
    for (UINT i = 0; i < N; i++)
        if (p[i] != i * 3 + 7) bad++;
    printf("compute readback: %s (p[0]=%u p[100]=%u)\n", bad ? "WRONG" : "OK", p[0], p[100]);
    rb->Unmap(0, nullptr);
    unsigned char* px = nullptr;
    D3D12_RANGE r2 = {0, 256 * 256 * 4};
    CHECK(rbTex->Map(0, &r2, (void**)&px));
    printf("clear readback: %u %u %u %u (expect ~64 128 191 255)\n", px[0], px[1], px[2], px[3]);
    rbTex->Unmap(0, nullptr);
    CHECK(sc->Present(1, 0));
    printf("present ok\nSMOKE PASS\n");
    return 0;
}
