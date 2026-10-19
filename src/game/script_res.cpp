#include "game/script_res.h"

#include <cstring>

namespace mc::game::script_res {
namespace {

bool Fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

template <typename T>
bool Get(const std::vector<uint8_t>& b, size_t& pos, T& v) {
    if (pos + sizeof(T) > b.size()) return false;
    std::memcpy(&v, b.data() + pos, sizeof(T));
    pos += sizeof(T);
    return true;
}

template <typename T>
void Put(std::vector<uint8_t>& b, T v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + sizeof(T));
}

}  // namespace

uint64_t Fnv1_64(const uint8_t* p, size_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    for (size_t i = 0; i < n; ++i) h = (h * 0x100000001B3ull) ^ p[i];
    return h;
}

bool Read(const std::vector<uint8_t>& b, Resource& r, std::string* error) {
    size_t pos = 0;
    uint16_t len = 0;
    uint32_t count = 0;
    r = Resource{};
    if (!Get(b, pos, r.word) || !Get(b, pos, len) || pos + len > b.size()) return Fail(error, "script resource header truncated");
    r.prefix.assign(reinterpret_cast<const char*>(b.data() + pos), len);
    pos += len;
    if (!Get(b, pos, count) || count > 4096) return Fail(error, "bad script count");
    for (uint32_t i = 0; i < count; ++i) {
        Script s;
        uint16_t nameLen = 0;
        uint32_t dataLen = 0;
        if (!Get(b, pos, s.nameHash) || !Get(b, pos, s.dataHash) || !Get(b, pos, nameLen) || pos + nameLen > b.size())
            return Fail(error, "script " + std::to_string(i) + ": header truncated");
        s.name.assign(reinterpret_cast<const char*>(b.data() + pos), nameLen);
        pos += nameLen;
        if (!Get(b, pos, dataLen) || pos + dataLen > b.size()) return Fail(error, "script " + std::to_string(i) + ": data truncated");
        s.data.assign(b.begin() + static_cast<std::ptrdiff_t>(pos), b.begin() + static_cast<std::ptrdiff_t>(pos + dataLen));
        pos += dataLen;
        r.scripts.push_back(std::move(s));
    }
    if (pos != b.size()) return Fail(error, std::to_string(b.size() - pos) + " bytes after the last script");
    return true;
}

std::vector<uint8_t> Write(const Resource& r) {
    std::vector<uint8_t> b;
    Put<uint32_t>(b, r.word);
    Put<uint16_t>(b, static_cast<uint16_t>(r.prefix.size()));
    b.insert(b.end(), r.prefix.begin(), r.prefix.end());
    Put<uint32_t>(b, static_cast<uint32_t>(r.scripts.size()));
    for (const auto& s : r.scripts) {
        Put<uint64_t>(b, s.nameHash);
        Put<uint64_t>(b, s.dataHash);
        Put<uint16_t>(b, static_cast<uint16_t>(s.name.size()));
        b.insert(b.end(), s.name.begin(), s.name.end());
        Put<uint32_t>(b, static_cast<uint32_t>(s.data.size()));
        b.insert(b.end(), s.data.begin(), s.data.end());
    }
    return b;
}

void UpdateHashes(Resource& r) {
    for (auto& s : r.scripts) {
        s.nameHash = Fnv1_64(s.name);
        s.dataHash = Fnv1_64(s.data.data(), s.data.size());
    }
}

}  // namespace mc::game::script_res
