// world.bin container (worldbin.h).
#include "worldbin.h"
#include "lz.h"
#include <cstring>

namespace World {

namespace worldbin_detail {

constexpr u32 kMagicNTWB = 0x4257544Eu;   // "NTWB" read as a little-endian u32
constexpr size_t kHeaderBytes = 24;
constexpr size_t kEntryBytes = 40;
constexpr size_t kNameBytes = 24;
constexpr size_t kAlign = 16;

void put32(std::vector<u8>& b, size_t at, u32 v) {
    b[at] = (u8)v;
    b[at + 1] = (u8)(v >> 8);
    b[at + 2] = (u8)(v >> 16);
    b[at + 3] = (u8)(v >> 24);
}
u32 get32(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }

}  // namespace worldbin_detail

u64 fnv1a64(const u8* data, size_t size, u64 h) {
    for (size_t i = 0; i < size; i++) h = (h ^ data[i]) * 0x100000001b3ull;
    return h;
}

void WorldBinWriter::add(const char* name, u32 kind, const std::vector<u8>& bytes) {
    Section s;
    s.name = name;
    s.kind = kind;
    s.bytes = bytes;
    sections.push_back(s);
}

void WorldBinWriter::addCompressed(const char* name, const std::vector<u8>& bytes) {
    std::vector<u8> z;
    ByteWriter w;
    w.putU32((u32)bytes.size());
    z = w.bytes;
    lzCompress(bytes.data(), bytes.size(), z);
    if (z.size() < bytes.size()) add(name, WSK_LZ, z);
    else add(name, WSK_RAW, bytes);
}

std::vector<u8> WorldBinWriter::finish() const {
    using namespace worldbin_detail;
    const size_t table = kHeaderBytes + sections.size() * kEntryBytes;
    size_t at = (table + kAlign - 1) / kAlign * kAlign;
    std::vector<u8> out(at, 0);
    std::vector<size_t> offsets;
    for (const Section& s : sections) {
        offsets.push_back(at);
        out.insert(out.end(), s.bytes.begin(), s.bytes.end());
        at += s.bytes.size();
        const size_t pad = (kAlign - at % kAlign) % kAlign;
        out.insert(out.end(), pad, 0);
        at += pad;
    }
    put32(out, 0, kMagicNTWB);
    put32(out, 4, kWorldBinVersion);
    put32(out, 8, (u32)sections.size());
    for (size_t i = 0; i < sections.size(); i++) {
        const size_t e = kHeaderBytes + i * kEntryBytes;
        const std::string& n = sections[i].name;
        for (size_t k = 0; k < kNameBytes - 1 && k < n.size(); k++) out[e + k] = (u8)n[k];
        put32(out, e + 24, (u32)offsets[i]);
        put32(out, e + 28, (u32)sections[i].bytes.size());
        put32(out, e + 32, sections[i].kind);
    }
    const size_t dataStart = (table + kAlign - 1) / kAlign * kAlign;
    const u64 h = fnv1a64(out.data() + dataStart, out.size() - dataStart);
    put32(out, 16, (u32)h);
    put32(out, 20, (u32)(h >> 32));
    return out;
}

bool WorldBinView::open(const u8* data, size_t size, std::string* error) {
    using namespace worldbin_detail;
    sections.clear();
    auto fail = [&](const char* why) {
        if (error) *error = why;
        sections.clear();
        return false;
    };
    if (!data || size < kHeaderBytes) return fail("too short for a world.bin header");
    if (get32(data) != kMagicNTWB) return fail("not a world.bin (no NTWB magic)");
    if (get32(data + 4) != kWorldBinVersion) return fail("world.bin version mismatch");
    const u32 count = get32(data + 8);
    if (count > 4096) return fail("too many sections");
    const size_t table = kHeaderBytes + (size_t)count * kEntryBytes;
    const size_t dataStart = (table + kAlign - 1) / kAlign * kAlign;
    if (size < dataStart) return fail("truncated section table");
    const u64 h = (u64)get32(data + 16) | ((u64)get32(data + 20) << 32);
    if (fnv1a64(data + dataStart, size - dataStart) != h) return fail("world.bin hash mismatch (damaged or truncated)");
    for (u32 i = 0; i < count; i++) {
        const u8* e = data + kHeaderBytes + (size_t)i * kEntryBytes;
        Section s;
        size_t len = 0;
        while (len < kNameBytes && e[len]) len++;
        if (len == 0 || len == kNameBytes) return fail("bad section name");
        s.name.assign((const char*)e, len);
        const u32 off = get32(e + 24), n = get32(e + 28);
        s.kind = get32(e + 32);
        if (off < dataStart || (size_t)off + n > size) return fail("section outside the file");
        s.data = data + off;
        s.size = n;
        sections.push_back(s);
    }
    return true;
}

const WorldBinView::Section* WorldBinView::find(const char* name) const {
    for (const Section& s : sections)
        if (s.name == name) return &s;
    return nullptr;
}

bool WorldBinView::bytes(const char* name, std::vector<u8>& out) const {
    const Section* s = find(name);
    out.clear();
    if (!s) return false;
    if (s->kind == WSK_RAW) {
        out.assign(s->data, s->data + s->size);
        return true;
    }
    if (s->kind != WSK_LZ || s->size < 4) return false;
    const u32 n = worldbin_detail::get32(s->data);
    if (n > (1u << 30)) return false;
    out.resize(n);
    return lzDecompress(s->data + 4, s->size - 4, out.data(), n);
}

void ByteWriter::putU32(u32 v) {
    bytes.push_back((u8)v);
    bytes.push_back((u8)(v >> 8));
    bytes.push_back((u8)(v >> 16));
    bytes.push_back((u8)(v >> 24));
}

void ByteWriter::putF32(float v) {
    u32 u;
    memcpy(&u, &v, 4);
    putU32(u);
}

void ByteWriter::putString(const std::string& s) {
    putU32((u32)s.size());
    bytes.insert(bytes.end(), s.begin(), s.end());
}

void ByteWriter::putPoints(const std::vector<vec2>& pts) {
    putU32((u32)pts.size());
    for (const vec2& p : pts) {
        putF32(p.x);
        putF32(p.y);
    }
}

u32 ByteReader::getU32() {
    if (bad || end - p < 4) {
        bad = true;
        return 0;
    }
    const u32 v = worldbin_detail::get32(p);
    p += 4;
    return v;
}

float ByteReader::getF32() {
    const u32 u = getU32();
    float v;
    memcpy(&v, &u, 4);
    return v;
}

std::string ByteReader::getString() {
    const u32 n = getU32();
    if (bad || (size_t)(end - p) < n) {
        bad = true;
        return std::string();
    }
    std::string s((const char*)p, n);
    p += n;
    return s;
}

void ByteReader::getPoints(std::vector<vec2>& pts) {
    pts.clear();
    const u32 n = getU32();
    if (bad || (size_t)(end - p) / 8 < n) {
        bad = true;
        return;
    }
    pts.reserve(n);
    for (u32 i = 0; i < n; i++) {
        const float x = getF32();
        const float y = getF32();
        pts.push_back(vec2(x, y));
    }
}

}  // namespace World
