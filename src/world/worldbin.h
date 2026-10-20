// world.bin: the compiled hand-built world (docs/HANDBUILT_WORLD.md, section 6). tools/worldc.cpp writes it from the
// text under world/, and the build embeds it in the exe as the RCDATA resource NT_WORLD.
//
// Layout, little-endian:
//   header   "NTWB" | version u32 | section count u32 | 0 u32 | FNV-1a 64 of every byte after this field (table and data)
//   table    per section: name (24 bytes, zero-padded) | offset u32 | size u32 | kind u32 | 0 u32
//   data     the sections, each starting on a 16-byte boundary
// Section kinds say how a section is stored; its name says what it holds ("terrain.height", ...).
#pragma once
#include "../core/math.h"
#include <string>
#include <vector>

namespace World {

constexpr u32 kWorldBinVersion = 1;

enum WorldSectionKind : u32 {
    WSK_RAW = 0,    // bytes as they are (the terrain record)
    WSK_GRID = 1,   // an NTG grid (gridcodec.h)
    WSK_LZ = 2,     // u32 size, then the bytes compressed with lz.h
};

struct WorldBinWriter {
    struct Section {
        std::string name;
        u32 kind = WSK_RAW;
        std::vector<u8> bytes;
    };
    std::vector<Section> sections;
    void add(const char* name, u32 kind, const std::vector<u8>& bytes);
    // Stores bytes as WSK_LZ, or as WSK_RAW when compressing doesn't make them smaller
    void addCompressed(const char* name, const std::vector<u8>& bytes);
    std::vector<u8> finish() const;
};

struct WorldBinView {
    struct Section {
        std::string name;
        u32 kind = 0;
        const u8* data = nullptr;
        size_t size = 0;
    };
    std::vector<Section> sections;
    // Checks the header, the table and the hash; false (with a reason) on any mismatch
    bool open(const u8* data, size_t size, std::string* error);
    const Section* find(const char* name) const;
    // A WSK_RAW or WSK_LZ section's bytes, decompressed; false when it is missing, of another kind or damaged
    bool bytes(const char* name, std::vector<u8>& out) const;
};

// Little-endian byte packing for section contents
struct ByteWriter {
    std::vector<u8> bytes;
    void putU32(u32 v);
    void putF32(float v);
    void putString(const std::string& s);          // u32 length, then the bytes
    void putPoints(const std::vector<vec2>& pts);  // u32 count, then x,y as f32
};

struct ByteReader {
    const u8* p = nullptr;
    const u8* end = nullptr;
    bool bad = false;
    ByteReader(const u8* data, size_t size) : p(data), end(data + size) {}
    u32 getU32();
    float getF32();
    std::string getString();
    void getPoints(std::vector<vec2>& pts);
    bool done() const { return !bad && p == end; }
};

u64 fnv1a64(const u8* data, size_t size, u64 h = 0xcbf29ce484222325ull);

}  // namespace World
