// Offline HLSL compile check for the Direct3D 12 build: shc51 <shader dir> <list file>
// List lines: file entry target [NAME=VALUE ...]. Same flags as gfx_shaders.cpp (O3, strictness, unbounded
// descriptor tables for the SM 5.1 bindless arrays).
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#ifndef D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES
#define D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES (1 << 20)
#endif
typedef HRESULT(WINAPI* PFN)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
typedef HRESULT(WINAPI* PFND)(LPCVOID, SIZE_T, UINT, LPCSTR, ID3DBlob**);
static std::string g_dir;
static bool readAll(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
struct Inc : public ID3DInclude {
    std::vector<std::string*> bufs;
    HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* bytes) override {
        std::string* s = new std::string();
        if (!readAll(g_dir + "/" + name, *s)) { delete s; return E_FAIL; }
        bufs.push_back(s);
        *data = s->data();
        *bytes = (UINT)s->size();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close(LPCVOID) override { return S_OK; }
    ~Inc() { for (auto* b : bufs) delete b; }
};
int main(int argc, char** argv) {
    if (argc < 3) { printf("usage: shc51 <shader dir> <list>\n"); return 2; }
    g_dir = argv[1];
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    PFN comp = m ? (PFN)(void*)GetProcAddress(m, "D3DCompile") : nullptr;
    if (!comp) { printf("d3dcompiler_47.dll not found\n"); return 2; }
    PFND dis = (PFND)(void*)GetProcAddress(m, "D3DDisassemble");
    const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_ENABLE_UNBOUNDED_DESCRIPTOR_TABLES;
    std::ifstream list(argv[2]);
    std::string line;
    int fails = 0, n = 0;
    while (std::getline(list, line)) {
        std::istringstream ls(line);
        std::string file, entry, target, def;
        if (!(ls >> file >> entry >> target)) continue;
        std::vector<std::string> names, vals;
        while (ls >> def) {
            size_t e = def.find('=');
            names.push_back(def.substr(0, e));
            vals.push_back(e == std::string::npos ? "1" : def.substr(e + 1));
        }
        std::vector<D3D_SHADER_MACRO> macros;
        for (size_t i = 0; i < names.size(); i++) macros.push_back({names[i].c_str(), vals[i].c_str()});
        macros.push_back({nullptr, nullptr});
        std::string src;
        if (!readAll(g_dir + "/" + file, src)) { printf("MISSING %s\n", file.c_str()); fails++; continue; }
        Inc inc;
        ID3DBlob *code = nullptr, *err = nullptr;
        HRESULT hr = comp(src.data(), src.size(), file.c_str(), macros.data(), &inc, entry.c_str(), target.c_str(), flags, 0, &code, &err);
        n++;
        if (FAILED(hr)) {
            fails++;
            printf("FAIL %s:%s (%s)\n%s\n", file.c_str(), entry.c_str(), target.c_str(), err ? (const char*)err->GetBufferPointer() : "");
        }
        if (code && dis) {
            ID3DBlob* text = nullptr;
            if (SUCCEEDED(dis(code->GetBufferPointer(), code->GetBufferSize(), 0, nullptr, &text)) && text) {
                const char* t = (const char*)text->GetBufferPointer();
                const char* q = strstr(t, "// Approximately ");
                int slots = q ? atoi(q + 17) : -1;
                int loads = 0;
                for (const char* r = t; (r = strstr(r, "ld_structured")) != nullptr; r++) loads++;
                printf("%-14s %-26s %5d slots, %3d ld_structured, %6u bytes\n", file.c_str(), entry.c_str(), slots, loads, (unsigned)code->GetBufferSize());
                text->Release();
            }
        }
        if (code) code->Release();
        if (err) err->Release();
    }
    printf("%d shaders, %d failed\n", n, fails);
    fflush(stdout);
    return fails ? 1 : 0;
}
