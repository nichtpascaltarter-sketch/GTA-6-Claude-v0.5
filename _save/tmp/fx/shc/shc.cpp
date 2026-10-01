// Offline HLSL compile checker: shc <shader dir> <list file>  (list lines: file entry target [NAME=VALUE])
#include <windows.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
typedef HRESULT(WINAPI* PFN)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
static std::string g_dir;
static bool readAll(const std::string& p, std::string& out) {
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true;
}
struct Inc : public ID3DInclude {
    std::vector<std::string*> bufs;
    HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR name, LPCVOID, LPCVOID* data, UINT* bytes) override {
        std::string* s = new std::string();
        if (!readAll(g_dir + "/" + name, *s)) { delete s; return E_FAIL; }
        bufs.push_back(s); *data = s->data(); *bytes = (UINT)s->size(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Close(LPCVOID) override { return S_OK; }
};
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    g_dir = argv[1];
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll");
    PFN comp = (PFN)(void*)GetProcAddress(m, "D3DCompile");
    std::ifstream list(argv[2]);
    std::string line; int fails = 0, n = 0;
    while (std::getline(list, line)) {
        std::istringstream ls(line);
        std::string file, entry, target, def;
        if (!(ls >> file >> entry >> target)) continue;
        std::vector<std::string> defs; while (ls >> def) defs.push_back(def);
        std::vector<std::string> names, vals; std::vector<D3D_SHADER_MACRO> macros;
        for (auto& d : defs) { size_t e = d.find('='); names.push_back(d.substr(0, e)); vals.push_back(e == std::string::npos ? "1" : d.substr(e + 1)); }
        for (size_t i = 0; i < names.size(); i++) macros.push_back({names[i].c_str(), vals[i].c_str()});
        macros.push_back({nullptr, nullptr});
        std::string src;
        if (!readAll(g_dir + "/" + file, src)) { printf("MISSING %s\n", file.c_str()); fails++; continue; }
        Inc inc; ID3DBlob *code = nullptr, *err = nullptr;
        HRESULT hr = comp(src.data(), src.size(), file.c_str(), macros.data(), &inc, entry.c_str(), target.c_str(),
                          D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_ENABLE_STRICTNESS, 0, &code, &err);
        n++;
        if (FAILED(hr)) { fails++; printf("FAIL %s:%s (%s)\n%s\n", file.c_str(), entry.c_str(), target.c_str(), err ? (const char*)err->GetBufferPointer() : ""); }
        else if (err && strstr((const char*)err->GetBufferPointer(), "warning")) printf("WARN %s:%s\n%s\n", file.c_str(), entry.c_str(), (const char*)err->GetBufferPointer());
        if (code) code->Release(); if (err) err->Release();
    }
    printf("%d shaders, %d failed\n", n, fails);
    fflush(stdout);
    return fails ? 1 : 0;
}
