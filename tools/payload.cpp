// The bundled compiler's tool (README: The bundled compiler; src/platform/update_logic.h), built and run by
// `build.bat bundle`. It hashes the pinned w64devkit download, keeps payload.txt next to payload.tgz and appends
// payload.tgz to an exe with the footer the updater reads:
//   payload sha256 FILE HEX               exit 0 when FILE's SHA-256 is HEX
//   payload record PAYLOAD RECORD LABEL   writes RECORD (payload.txt: size, SHA-256 and LABEL) for PAYLOAD
//   payload check PAYLOAD RECORD LABEL    exit 0 when RECORD describes PAYLOAD (size and SHA-256) with that label
//   payload attach EXE PAYLOAD RECORD     EXE then carries PAYLOAD (checked against RECORD) instead of what it had
//   payload show EXE [--verify]           what EXE carries (with --verify: its SHA-256 checked); exit 1 for nothing
// A failure prints why and exits 1; an exe that cannot be given the payload is left as it was built.
#include "../src/platform/update_logic.cpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#endif
#endif

namespace fs = std::filesystem;
using namespace Updater;

namespace {

int fail(const std::string& why) {
    std::fprintf(stderr, "payload: %s\n", why.c_str());
    return 1;
}

std::string name(const fs::path& p) { return p.u8string().c_str(); }

bool fileSize(const fs::path& p, uint64_t& size) {
    std::error_code ec;
    size = (uint64_t)fs::file_size(p, ec);
    return !ec;
}

// SHA-256 of `size` bytes of `in` from where it is, copied to `out` when there is one
bool hashStream(std::istream& in, uint64_t size, std::ostream* out, std::string& hex) {
    PortableSha256 h;
    std::vector<char> buf(1 << 20);
    while (size > 0) {
        size_t n = (size_t)std::min<uint64_t>(size, buf.size());
        if (!in.read(buf.data(), (std::streamsize)n)) return false;
        h.add(buf.data(), n);
        if (out && !out->write(buf.data(), (std::streamsize)n)) return false;
        size -= n;
    }
    hex = h.hex();
    return true;
}

bool hashFile(const fs::path& p, uint64_t& size, std::string& hex) {
    std::ifstream in(p, std::ios::binary);
    return in && fileSize(p, size) && hashStream(in, size, nullptr, hex);
}

bool readRecord(const fs::path& p, Payload& rec) {
    std::ifstream in(p, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return in.good() || in.eof() ? parsePayloadRecord(text, rec) : false;
}

// What an exe carries: the footer when it has one (`core`: the exe's own bytes in front of the payload)
bool readFooter(const fs::path& exe, uint64_t& total, uint64_t& core, Payload& p, bool& has) {
    has = false;
    if (!fileSize(exe, total)) return false;
    core = total;
    std::ifstream in(exe, std::ios::binary);
    if (!in) return false;
    char mz[2] = {};
    if (total < kPayloadMinExeBytes || !in.read(mz, 2) || mz[0] != 'M' || mz[1] != 'Z') return false;
    uint8_t tail[kPayloadFooterBytes];
    in.seekg((std::streamoff)(total - kPayloadFooterBytes));
    if (!in.read((char*)tail, sizeof(tail))) return false;
    has = parsePayloadFooter(tail, sizeof(tail), total, p);
    if (has) core = total - kPayloadFooterBytes - p.size;
    return true;
}

int cmdSha256(const fs::path& file, const std::string& want) {
    uint64_t size = 0;
    std::string hex;
    if (!hashFile(file, size, hex)) return fail("cannot read " + name(file));
    std::printf("%s  %s\n", hex.c_str(), name(file).c_str());
    if (hex != lower(want)) return fail(name(file) + " does not have the SHA-256 " + want);
    return 0;
}

int cmdRecord(const fs::path& payload, const fs::path& record, const std::string& label) {
    Payload p;
    p.label = label;
    if (!payloadLabelValid(label)) return fail("the label must be 1 to 48 printable ASCII characters");
    if (!hashFile(payload, p.size, p.sha256) || p.size == 0) return fail("cannot read " + name(payload));
    std::ofstream out(record, std::ios::binary | std::ios::trunc);
    if (!(out << payloadRecord(p)) || !(out.close(), out)) return fail("cannot write " + name(record));
    std::printf("%s: %s, %.1f MB, SHA-256 %s\n", name(payload).c_str(), label.c_str(), (double)p.size / 1048576.0, p.sha256.c_str());
    return 0;
}

int cmdCheck(const fs::path& payload, const fs::path& record, const std::string& label) {
    Payload rec;
    uint64_t size = 0;
    std::string hex;
    if (!readRecord(record, rec)) return fail("no readable " + name(record));
    if (rec.label != label) return fail(name(record) + " is for " + rec.label + ", not " + label);
    if (!fileSize(payload, size) || size != rec.size) return fail(name(payload) + " is missing or not the size " + name(record) + " says");
    if (!hashFile(payload, size, hex) || hex != rec.sha256) return fail(name(payload) + " does not have the SHA-256 " + name(record) + " says");
    std::printf("%s: %s, %.1f MB\n", name(payload).c_str(), label.c_str(), (double)size / 1048576.0);
    return 0;
}

int cmdAttach(const fs::path& exe, const fs::path& payload, const fs::path& record) {
    Payload rec, old;
    uint64_t total = 0, core = 0, size = 0;
    bool has = false;
    if (!readRecord(record, rec)) return fail("no readable " + name(record));
    if (!fileSize(payload, size) || size != rec.size) return fail(name(payload) + " is missing or not the size " + name(record) + " says");
    if (!readFooter(exe, total, core, old, has)) return fail(name(exe) + " is not an exe that can be read");
    std::error_code ec;
    if (has) {   // what it carried goes first; the exe in front of it stays as it is
        fs::resize_file(exe, core, ec);
        if (ec) return fail("cannot take the old payload off " + name(exe));
    }
    std::string hex, footer = payloadFooter(rec);
    bool ok = false;
    {
        std::ifstream in(payload, std::ios::binary);
        std::ofstream out(exe, std::ios::binary | std::ios::app);
        ok = in && out && hashStream(in, size, &out, hex) && hex == rec.sha256 && !footer.empty() &&
             out.write(footer.data(), (std::streamsize)footer.size()) && (out.close(), (bool)out);
    }
    if (!ok) {
        fs::resize_file(exe, core, ec);   // back to the exe as built
        return fail(hex.empty() || hex == rec.sha256 ? "cannot append " + name(payload) + " to " + name(exe)
                                                     : name(payload) + " does not have the SHA-256 " + name(record) + " says");
    }
    std::printf("%s carries %s now (%.1f MB, SHA-256 %s): %.1f MB in all\n", name(exe).c_str(), rec.label.c_str(),
                (double)rec.size / 1048576.0, rec.sha256.c_str(), (double)(core + rec.size + kPayloadFooterBytes) / 1048576.0);
    return 0;
}

int cmdShow(const fs::path& exe, bool verify) {
    Payload p;
    uint64_t total = 0, core = 0;
    bool has = false;
    if (!readFooter(exe, total, core, p, has)) return fail(name(exe) + " is not an exe that can be read");
    if (!has) {
        std::printf("%s carries no bundled compiler (%.1f MB)\n", name(exe).c_str(), (double)total / 1048576.0);
        return 1;
    }
    if (verify) {
        std::ifstream in(exe, std::ios::binary);
        std::string hex;
        in.seekg((std::streamoff)core);
        if (!in || !hashStream(in, p.size, nullptr, hex)) return fail("cannot read the payload of " + name(exe));
        if (hex != p.sha256) return fail("the payload of " + name(exe) + " does not have the SHA-256 its footer says");
    }
    std::printf("%s carries %s: %llu bytes at %llu, SHA-256 %s%s\n", name(exe).c_str(), p.label.c_str(), (unsigned long long)p.size,
                (unsigned long long)core, p.sha256.c_str(), verify ? " (checked)" : "");
    return 0;
}

int run(const std::vector<fs::path>& a) {
    std::string cmd = a.size() > 1 ? name(a[1]) : std::string();
    if (cmd == "sha256" && a.size() == 4) return cmdSha256(a[2], name(a[3]));
    if (cmd == "record" && a.size() == 5) return cmdRecord(a[2], a[3], name(a[4]));
    if (cmd == "check" && a.size() == 5) return cmdCheck(a[2], a[3], name(a[4]));
    if (cmd == "attach" && a.size() == 5) return cmdAttach(a[2], a[3], a[4]);
    if (cmd == "show" && (a.size() == 3 || (a.size() == 4 && name(a[3]) == "--verify"))) return cmdShow(a[2], a.size() == 4);
    return fail("usage: payload sha256 FILE HEX | record PAYLOAD RECORD LABEL | check PAYLOAD RECORD LABEL | "
                "attach EXE PAYLOAD RECORD | show EXE [--verify]");
}

}  // namespace

#ifdef _WIN32
int main() {   // (the arguments as UTF-16, for folders with any name)
    int n = 0;
    wchar_t** w = CommandLineToArgvW(GetCommandLineW(), &n);
    if (!w) return fail("no command line");
    std::vector<fs::path> a(w, w + n);
    LocalFree(w);
    return run(a);
}
#else
int main(int argc, char** argv) { return run(std::vector<fs::path>(argv, argv + argc)); }
#endif
