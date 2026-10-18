#pragma once
// Mafia II's SDS archives (version 19, PC), as far as this mod needs them: read one into its resources, change some
// resources' data, write it back. Layout found by reading the game's own files (docs/re-notes.md 3):
//   header   "SDS\0", u32 version 19, "PC\0\0", u32 FNV-1 32 of the first 12 bytes,
//            @0x10 u32 type table offset, u32 block stream offset, u32 XML index offset,
//            u32 slot RAM, slot VRAM, other RAM, other VRAM (sums over the resources), u32 1, 16 bytes 0,
//            @0x40 u32 resource count, @0x44 u32 FNV-1 32 of bytes 0x10..0x43
//   types    u32 count, then { u32 id, u32 length, name, u32 parent }
//   blocks   "UEzl", u32 alignment (0x4000), u8 4, then { u32 size, u8 compressed, data }..., u32 0, u8 0.
//            A compressed block has a 32-byte header (u32 uncompressed size, u32 header size 0x20, ...) followed
//            by a zlib stream; a raw block is its data. Concatenated, they are the resource stream.
//   stream   per resource: u32 type id, u32 size (with this 26-byte header), u16 version, u32 slot RAM, slot VRAM,
//            other RAM, other VRAM, then size - 26 bytes of data
//   index    an XML text listing every resource (type name, source name, memory needs), in stream order
#include <cstdint>
#include <string>
#include <vector>

namespace mc::game::sds {

struct Resource {
    uint32_t typeId = 0;
    uint16_t version = 0;
    uint32_t ram[4] = {};       // slot RAM, slot VRAM, other RAM, other VRAM
    std::vector<uint8_t> data;  // without the 26-byte header
    std::string typeName;       // from the type table
    std::string sourceName;     // from the XML index (e.g. "/config/gui/Screens/MainMenu/MainMenu")
};

struct Archive {
    std::vector<uint8_t> header;     // the first 0x48 bytes as read
    std::vector<uint8_t> typeTable;  // as read
    std::vector<uint8_t> index;      // the XML index text as read
    uint32_t alignment = 0x4000;
    std::vector<Resource> resources;

    Resource* Find(const std::string& sourceName);
};

// Parses a whole archive file image. False (with a reason) if it is not a version-19 PC archive we understand.
bool Read(const std::vector<uint8_t>& file, Archive& out, std::string* error);

// Serialises the archive: same header fields except the offsets, the RAM sums and the header hash; the block stream
// is written as raw (uncompressed) blocks of `alignment` bytes, which the game also uses in its own archives.
std::vector<uint8_t> Write(const Archive& a);

uint32_t Fnv1_32(const uint8_t* p, size_t n);

}  // namespace mc::game::sds
