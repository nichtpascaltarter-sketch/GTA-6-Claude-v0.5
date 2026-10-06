// Shader compilation (embedded HLSL -> SM 5.1 DXBC through d3dcompiler_47, cached on disk by source hash),
// reflection of the resources each shader uses, root signatures and pipeline state objects. Included from gfx.cpp.
namespace gfx {
namespace {

typedef HRESULT(WINAPI* PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT,
                                         UINT, ID3DBlob**, ID3DBlob**);
typedef HRESULT(WINAPI* PFN_D3DReflect)(LPCVOID, SIZE_T, REFIID, void**);
PFN_D3DCompile pD3DCompile = nullptr;
PFN_D3DReflect pD3DReflect = nullptr;
u64 g_bundleHash = 0;
std::atomic<int> g_compileCount{0};
std::atomic<long long> g_compileMicros{0};
std::string g_cacheDir;
u32 g_shaderIds = 0;
std::vector<ShaderObj*> g_shaders;

const EmbeddedFile* findEmbedded(const char* name) {
    for (int i = 0; i < g_embeddedShaderCount; i++)
        if (strcmp(g_embeddedShaders[i].name, name) == 0) return &g_embeddedShaders[i];
    return nullptr;
}

struct EmbeddedInclude : public ID3DInclude {
    HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR fileName, LPCVOID, LPCVOID* ppData, UINT* pBytes) override {
        const char* name = fileName;
        const char* s = strrchr(name, '/');
        if (s) name = s + 1;
        s = strrchr(name, '\\');
        if (s) name = s + 1;
        const EmbeddedFile* f = findEmbedded(name);
        if (!f) {
            LOG("Shader include not found: %s", fileName);
            return E_FAIL;
        }
        *ppData = f->data;
        *pBytes = f->size;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close(LPCVOID) override { return S_OK; }
};

bool readFile(const std::string& path, std::vector<u8>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) {
        fclose(f);
        return false;
    }
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

const UINT kCompileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;

std::vector<u8> compileCode(const char* file, const char* entry, const char* target, const std::vector<ShaderDefine>& defines) {
    const EmbeddedFile* f = findEmbedded(file);
    if (!f) FatalError("Shader file not embedded: %s", file);
    std::string key = std::string(file) + "|" + entry + "|" + target + StrFormat("|%08x", kCompileFlags);
    for (auto& d : defines) key += std::string("|") + d.name + "=" + (d.value ? d.value : "");
    u64 h = hash64(key.data(), key.size()) ^ (g_bundleHash * 0x9E3779B97F4A7C15ULL);
    char hname[64];
    snprintf(hname, sizeof(hname), "%016llx.cso", (unsigned long long)h);
    std::string cachePath = g_cacheDir + hname;
    std::vector<u8> code;
    if (!Platform::hasArg("nocache") && readFile(cachePath, code) && code.size() > 32 && memcmp(code.data(), "DXBC", 4) == 0) return code;
    std::vector<D3D_SHADER_MACRO> macros;
    for (auto& d : defines) macros.push_back({d.name, d.value ? d.value : "1"});
    macros.push_back({nullptr, nullptr});
    EmbeddedInclude inc;
    ID3DBlob *blob = nullptr, *err = nullptr;
    double t0 = Platform::timeSeconds();
    HRESULT hr = pD3DCompile(f->data, f->size, file, macros.data(), &inc, entry, target, kCompileFlags, 0, &blob, &err);
    double took = Platform::timeSeconds() - t0;
    g_compileMicros += (long long)(took * 1e6);
    g_compileCount++;
    LOG("Compiled %s:%s (%s) in %.2f s", file, entry, target, took);
    if (FAILED(hr)) {
        const char* msg = err ? (const char*)err->GetBufferPointer() : "(no message)";
        LOG("Shader compile error %s:%s (%s):\n%s", file, entry, target, msg);
        SAFE_RELEASE(err);
        FatalError("Failed to compile shader %s:%s. See log.txt for details.", file, entry);
    }
    if (err) {
        const char* msg = (const char*)err->GetBufferPointer();
        if (msg && strstr(msg, "warning") && Platform::hasArg("shaderwarnings")) LOG("Shader warnings %s:%s:\n%s", file, entry, msg);
        SAFE_RELEASE(err);
    }
    code.assign((const u8*)blob->GetBufferPointer(), (const u8*)blob->GetBufferPointer() + blob->GetBufferSize());
    blob->Release();
    if (FILE* out = fopen(cachePath.c_str(), "wb")) {
        fwrite(code.data(), 1, code.size(), out);
        fclose(out);
    }
    return code;
}

// Which default-model slots the shader reads (and what kind of resource each is), so draws copy only those into
// their descriptor tables and unbound slots get null descriptors of the right kind.
void reflectShader(ShaderObj* s, const char* file, const char* entry) {
    ID3D12ShaderReflection* refl = nullptr;
    if (!pD3DReflect || FAILED(pD3DReflect(s->code.data(), s->code.size(), __uuidof(ID3D12ShaderReflection), (void**)&refl)) || !refl)
        FatalError("Shader reflection failed for %s:%s", file, entry);
    D3D12_SHADER_DESC sd = {};
    refl->GetDesc(&sd);
    ShaderBindings& b = s->bind;
    for (UINT i = 0; i < sd.BoundResources; i++) {
        D3D12_SHADER_INPUT_BIND_DESC bd = {};
        if (FAILED(refl->GetResourceBindingDesc(i, &bd))) continue;
        if (bd.Type == D3D_SIT_SAMPLER) {
            if (bd.Space != 0 || bd.BindPoint + Max(bd.BindCount, 1u) > 7)
                FatalError("Shader %s:%s declares sampler s%u (space %u) outside the fixed set s0..s6", file, entry, bd.BindPoint, bd.Space);
            continue;
        }
        if (bd.Space == kRootConstantSpace) {
            if (bd.Type == D3D_SIT_CBUFFER && bd.BindPoint == 0) b.rootConstants = true;
            else FatalError("Shader %s:%s: register space %u only holds the root constants (b0)", file, entry, kRootConstantSpace);
            continue;
        }
        if (bd.Space != 0) {
            b.bindless = true;
            bool uav = bd.Type == D3D_SIT_UAV_RWTYPED || bd.Type == D3D_SIT_UAV_RWSTRUCTURED || bd.Type == D3D_SIT_UAV_RWBYTEADDRESS ||
                       bd.Type == D3D_SIT_UAV_APPEND_STRUCTURED || bd.Type == D3D_SIT_UAV_CONSUME_STRUCTURED ||
                       bd.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER;
            if (bd.Type == D3D_SIT_CBUFFER || (!uav && bd.Space > kBindlessSRVSpaces) || (uav && bd.Space > kBindlessUAVSpaces))
                FatalError("Shader %s:%s: %s in register space %u is outside the bindless spaces", file, entry, bd.Name, bd.Space);
            continue;
        }
        u32 count = Max(bd.BindCount, 1u);
        switch (bd.Type) {
            case D3D_SIT_CBUFFER:
                if (bd.BindPoint + count > kMaxCBSlots) FatalError("Shader %s:%s: constant buffer b%u is outside b0..b3", file, entry, bd.BindPoint);
                for (u32 k = 0; k < count; k++) b.cbMask |= (u8)(1u << (bd.BindPoint + k));
                break;
            case D3D_SIT_TBUFFER:
            case D3D_SIT_TEXTURE:
            case D3D_SIT_STRUCTURED:
            case D3D_SIT_BYTEADDRESS:
                if (bd.BindPoint + count > kMaxSRVSlots) FatalError("Shader %s:%s: t%u is outside t0..t47", file, entry, bd.BindPoint);
                for (u32 k = 0; k < count; k++) {
                    u32 slot = bd.BindPoint + k;
                    b.srvMask |= 1ull << slot;
                    b.srvType[slot] = (u8)bd.Type;
                    b.srvDim[slot] = (u8)bd.Dimension;
                    b.srvStride[slot] = (u16)(bd.Type == D3D_SIT_STRUCTURED ? bd.NumSamples : 0);
                }
                break;
            default:   // UAVs
                if (bd.BindPoint + count > kMaxUAVSlots) FatalError("Shader %s:%s: u%u is outside u0..u7", file, entry, bd.BindPoint);
                if (s->stage != STAGE_CS) FatalError("Shader %s:%s: UAVs are only bound for compute shaders", file, entry);
                for (u32 k = 0; k < count; k++) {
                    u32 slot = bd.BindPoint + k;
                    b.uavMask |= (u8)(1u << slot);
                    b.uavType[slot] = (u8)bd.Type;
                    b.uavDim[slot] = (u8)bd.Dimension;
                    bool structured = bd.Type == D3D_SIT_UAV_RWSTRUCTURED || bd.Type == D3D_SIT_UAV_APPEND_STRUCTURED ||
                                      bd.Type == D3D_SIT_UAV_CONSUME_STRUCTURED || bd.Type == D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER;
                    b.uavStride[slot] = (u16)(structured ? bd.NumSamples : 0);
                }
                break;
        }
    }
    refl->Release();
}

ShaderObj* makeShader(Stage stage, const char* file, const char* entry, const char* target, const std::vector<ShaderDefine>& defines) {
    ShaderObj* s = new ShaderObj();
    s->stage = stage;
    s->code = compileCode(file, entry, target, defines);
    s->id = ++g_shaderIds;
    s->name = std::string(file) + ":" + entry;
    for (auto& d : defines) s->name += std::string(" ") + d.name + "=" + (d.value ? d.value : "1");
    reflectShader(s, file, entry);
    g_shaders.push_back(s);
    return s;
}

}  // namespace

bool shaderCompilerInit() {
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    if (!m) {
        LOG("d3dcompiler_47.dll not found");
        return false;
    }
    pD3DCompile = (PFN_D3DCompile)(void*)GetProcAddress(m, "D3DCompile");
    pD3DReflect = (PFN_D3DReflect)(void*)GetProcAddress(m, "D3DReflect");
    if (!pD3DCompile || !pD3DReflect) return false;
    u64 h = 1469598103934665603ULL;
    for (int i = 0; i < g_embeddedShaderCount; i++) {
        h ^= hash64(g_embeddedShaders[i].data, g_embeddedShaders[i].size);
        h *= 1099511628211ULL;
    }
    g_bundleHash = h;
    g_cacheDir = Platform::userDataDir() + "shadercache\\";
    CreateDirectoryA(g_cacheDir.c_str(), nullptr);
    return true;
}

VertexShader loadVS(const char* file, const char* entry, const InputElement* layout, int layoutCount, const std::vector<ShaderDefine>& defines) {
    VertexShader v;
    v.vs = makeShader(STAGE_VS, file, entry, "vs_5_1", defines);
    v.layout = createInputLayout(layout, layoutCount);
    return v;
}

PixelShader loadPS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines) {
    return makeShader(STAGE_PS, file, entry, "ps_5_1", defines);
}

ComputeShader loadCS(const char* file, const char* entry, const std::vector<ShaderDefine>& defines) {
    ShaderObj* s = makeShader(STAGE_CS, file, entry, "cs_5_1", defines);
    D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
    pd.pRootSignature = g.rsCompute;
    pd.CS = {s->code.data(), s->code.size()};
    double t0 = Platform::timeSeconds();
    HRESULT hr = g.dev->CreateComputePipelineState(&pd, __uuidof(ID3D12PipelineState), (void**)&s->computePSO);
    g.psoSeconds += Platform::timeSeconds() - t0;
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED) deviceLost("compute pipeline creation", hr);
        FatalError("Direct3D 12: compute pipeline for %s failed (%08lx)", s->name.c_str(), (unsigned long)hr);
    }
    wchar_t w[128];
    MultiByteToWideChar(CP_UTF8, 0, s->name.c_str(), -1, w, 128);
    s->computePSO->SetName(w);
    return s;
}

void releaseShader(ShaderObj* s) {
    if (!s) return;
    if (ctx) {
        if (ctx->list()) ctx->flushBarriers();
    }
    if (s->computePSO) deferRelease(s->computePSO);
    s->computePSO = nullptr;
    // graphics pipelines built with it are never looked up again (ids are unique): release them too
    for (auto it = g.psoCache.begin(); it != g.psoCache.end();) {
        if (it->first.vs == s->id || it->first.ps == s->id) {
            deferRelease(it->second);
            it = g.psoCache.erase(it);
        } else ++it;
    }
    auto it = std::find(g_shaders.begin(), g_shaders.end(), s);
    if (it != g_shaders.end()) g_shaders.erase(it);
    if (ctx) ctx->onShaderRelease(s);
    delete s;
}

int shaderCompileCount() { return g_compileCount.load(); }
double shaderCompileSeconds() { return g_compileMicros.load() * 1e-6; }
int pipelineCount() { return g.psoCount; }
double pipelineSeconds() { return g.psoSeconds; }

ID3D12RootSignature* createRootSignature(const D3D12_ROOT_SIGNATURE_DESC& d) {
    ID3DBlob *blob = nullptr, *err = nullptr;
    HRESULT hr = pD3D12SerializeRootSignature(&d, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err);
    if (FAILED(hr)) {
        LOG("Root signature serialization failed: %s", err ? (const char*)err->GetBufferPointer() : "(no message)");
        SAFE_RELEASE(err);
        FatalError("Direct3D 12: root signature serialization failed (%08lx)", (unsigned long)hr);
    }
    SAFE_RELEASE(err);
    ID3D12RootSignature* rs = nullptr;
    hr = g.dev->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&rs);
    blob->Release();
    checkHR(hr, "CreateRootSignature");
    return rs;
}

ID3D12RootSignature* defaultGraphicsRootSignature() { return g.rsGraphics; }
ID3D12RootSignature* defaultComputeRootSignature() { return g.rsCompute; }

}  // namespace gfx
