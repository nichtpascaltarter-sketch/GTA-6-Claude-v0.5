// Lossless codec for the baked terrain grids of the hand-built world (docs/HANDBUILT_WORLD.md, sections 3.2 and 6.1):
// heights in cm, the water class, the region map and the coarse coast distance.
//
// After JPEG-LS (LOCO-I): every value is predicted from its left, upper and upper-left neighbours (the median edge
// detector), and the residual is Golomb-Rice coded with a parameter that adapts per context of local activity. Flat
// stretches (left, up-left, up and up-right all equal) are coded as runs. Deterministic and portable: the stream is
// defined bit for bit, so a grid encoded on the build machine decodes the same in the game.
#pragma once
#include "../core/base.h"
#include <vector>

namespace World {

enum GridType : u8 { GRID_I16 = 1, GRID_U8 = 2 };

struct GridInfo {
    int width = 0, height = 0;
    u8 type = 0;
    u64 hash = 0;              // FNV-1a 64 of the values, as 16-bit little-endian words
    size_t payloadBytes = 0;   // the bitstream after the 32-byte header
};

constexpr size_t kGridHeaderBytes = 32;

// Encodes width * height values (row-major; for GRID_U8 every value must be in 0..255) into `out`, replacing its
// contents: the header, then the bitstream. Returns false on bad arguments.
bool encodeGrid(const i16* values, int width, int height, GridType type, std::vector<u8>& out);
// Decodes what encodeGrid wrote, checking the header, the stream and the hash. Returns false on any mismatch.
bool decodeGrid(const u8* data, size_t size, std::vector<i16>& values, GridInfo* info);
// Reads and checks the header only.
bool readGridInfo(const u8* data, size_t size, GridInfo* info);
u64 gridHash(const i16* values, size_t count);

}  // namespace World
