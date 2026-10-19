#pragma once
// The game's "Script" SDS resources (pc\sds\missionscript\*.sds, pc\sds\script\*.sds): one or more Lua chunks under a
// path prefix. Layout found from the game's own files (docs/re-notes.md 5):
//   u32  prefix word (unknown; like the XML resources' word it depends on the resource's size, not its content:
//        script resources of the same size share it - so it is kept and the size must not change)
//   u16  length + path prefix ("/scripts/missions/FreerideMain/")
//   u32  number of scripts, then per script:
//        u64 FNV-1 64 of the name, u64 FNV-1 64 of the data, u16 length + name
//        ("/scripts/missions/FreerideMain/FreerideScript.lua"), u32 length + data (a Lua 5.1 chunk, game/lua51.h)
#include <cstdint>
#include <string>
#include <vector>

namespace mc::game::script_res {

struct Script {
    uint64_t nameHash = 0, dataHash = 0;  // as stored
    std::string name;
    std::vector<uint8_t> data;
};

struct Resource {
    uint32_t word = 0;
    std::string prefix;
    std::vector<Script> scripts;
};

bool Read(const std::vector<uint8_t>& data, Resource& out, std::string* error);
// Writes the resource back; the hashes are written as stored (call UpdateHashes after changing a name or data).
std::vector<uint8_t> Write(const Resource& r);
// Recomputes both hashes of every script from its name and data.
void UpdateHashes(Resource& r);

uint64_t Fnv1_64(const uint8_t* p, size_t n);
inline uint64_t Fnv1_64(const std::string& s) { return Fnv1_64(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

}  // namespace mc::game::script_res
