// A small LZ77 codec for world.bin's sections (docs/HANDBUILT_WORLD.md, section 6.1): the LZ4 block format's idea in our
// own code, a hash-chain encoder and a bounds-checked decoder of about 50 lines that runs at memory speed.
//
// Stream: sequences of [token][literal length bytes][literals][offset u16][match length bytes]. The token's high nibble
// is the literal count, its low nibble the match length minus 4; a nibble of 15 continues in bytes of 255... ending
// with one below 255. The offset counts back from the current position (1..65535). The last sequence has literals only
// and no offset.
#pragma once
#include "../core/base.h"
#include <vector>

namespace World {

// Appends the compressed form of src to out
void lzCompress(const u8* src, size_t size, std::vector<u8>& out);
// Decompresses exactly dstSize bytes; false when the stream is malformed or doesn't fill dst exactly
bool lzDecompress(const u8* src, size_t size, u8* dst, size_t dstSize);

}  // namespace World
