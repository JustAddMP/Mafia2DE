#pragma once
// A small zlib (RFC 1950) / deflate (RFC 1951) decoder, written from the RFCs, for the compressed blocks of the
// game's SDS archives (src/game/sds.h). Decoding only; the archives we write use raw (stored) blocks.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace mc::game {

// Inflates one zlib stream (2-byte header, deflate data, Adler-32) and appends the output to `out`. False on any
// format error or a wrong checksum. `used` (optional) gets the number of input bytes consumed.
bool ZlibInflate(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t* used = nullptr);

// Inflates raw deflate data (no zlib wrapper). False on a format error.
bool Inflate(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t* used = nullptr);

uint32_t Adler32(const uint8_t* p, size_t n, uint32_t adler = 1);

}  // namespace mc::game
