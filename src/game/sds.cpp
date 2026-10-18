#include "game/sds.h"

#include "game/inflate.h"

#include <cstring>
#include <map>

namespace mc::game::sds {
namespace {

uint32_t U32(const std::vector<uint8_t>& b, size_t o) {
    uint32_t v;
    std::memcpy(&v, b.data() + o, 4);
    return v;
}
uint16_t U16(const std::vector<uint8_t>& b, size_t o) {
    uint16_t v;
    std::memcpy(&v, b.data() + o, 2);
    return v;
}
void Put32(std::vector<uint8_t>& b, uint32_t v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + 4);
}
void Put16(std::vector<uint8_t>& b, uint16_t v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + 2);
}
void Set32(std::vector<uint8_t>& b, size_t o, uint32_t v) { std::memcpy(b.data() + o, &v, 4); }

bool Fail(std::string* error, const std::string& why) {
    if (error) *error = why;
    return false;
}

// The <TypeName> / <SourceDataDescription> pairs of the index, in order.
std::vector<std::pair<std::string, std::string>> IndexEntries(const std::string& x) {
    std::vector<std::pair<std::string, std::string>> out;
    size_t pos = 0;
    for (;;) {
        const size_t t = x.find("<TypeName>", pos);
        if (t == std::string::npos) break;
        const size_t te = x.find("</TypeName>", t);
        const size_t s = x.find("<SourceDataDescription>", te);
        const size_t se = s == std::string::npos ? s : x.find("</SourceDataDescription>", s);
        if (te == std::string::npos || se == std::string::npos) break;
        out.emplace_back(x.substr(t + 10, te - t - 10), x.substr(s + 23, se - s - 23));
        pos = se;
    }
    return out;
}

}  // namespace

uint32_t Fnv1_32(const uint8_t* p, size_t n) {
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < n; ++i) h = (h * 0x01000193u) ^ p[i];
    return h;
}

Resource* Archive::Find(const std::string& sourceName) {
    for (auto& r : resources)
        if (r.sourceName == sourceName) return &r;
    return nullptr;
}

bool Read(const std::vector<uint8_t>& f, Archive& a, std::string* error) {
    if (f.size() < 0x48 || std::memcmp(f.data(), "SDS\0", 4) != 0) return Fail(error, "not an SDS file");
    if (U32(f, 4) != 19 || std::memcmp(f.data() + 8, "PC\0\0", 4) != 0) return Fail(error, "not a version 19 PC SDS");
    const uint32_t typeOff = U32(f, 0x10), blockOff = U32(f, 0x14), indexOff = U32(f, 0x18);
    const uint32_t count = U32(f, 0x40);
    if (typeOff != 0x48 || blockOff <= typeOff || indexOff <= blockOff || indexOff > f.size())
        return Fail(error, "unexpected header offsets");
    a.header.assign(f.begin(), f.begin() + 0x48);
    a.typeTable.assign(f.begin() + typeOff, f.begin() + blockOff);
    a.index.assign(f.begin() + indexOff, f.end());

    // type table
    std::map<uint32_t, std::string> types;
    {
        size_t o = typeOff;
        const uint32_t n = U32(f, o);
        o += 4;
        for (uint32_t i = 0; i < n; ++i) {
            if (o + 8 > blockOff) return Fail(error, "type table truncated");
            const uint32_t id = U32(f, o), len = U32(f, o + 4);
            o += 8;
            if (o + len + 4 > blockOff) return Fail(error, "type table truncated");
            types[id] = std::string(reinterpret_cast<const char*>(f.data() + o), len);
            o += len + 4;
        }
    }

    // block stream
    if (std::memcmp(f.data() + blockOff, "UEzl", 4) != 0) return Fail(error, "no UEzl block stream");
    a.alignment = U32(f, blockOff + 4);
    std::vector<uint8_t> stream;
    size_t o = blockOff + 9;
    for (;;) {
        if (o + 5 > indexOff) return Fail(error, "block stream truncated");
        const uint32_t size = U32(f, o);
        o += 4;
        if (size == 0) break;
        const uint8_t compressed = f[o++];
        if (o + size > indexOff) return Fail(error, "block past the end");
        if (compressed) {
            if (size < 0x20) return Fail(error, "compressed block too small");
            const uint32_t unc = U32(f, o), hdr = U32(f, o + 4);
            if (hdr < 8 || hdr > size) return Fail(error, "bad compressed block header");
            const size_t before = stream.size();
            if (!ZlibInflate(f.data() + o + hdr, size - hdr, stream))
                return Fail(error, "zlib error in block at " + std::to_string(o));
            if (stream.size() - before != unc) return Fail(error, "block size mismatch at " + std::to_string(o));
        } else {
            stream.insert(stream.end(), f.begin() + o, f.begin() + o + size);
        }
        o += size;
    }

    // resources
    const std::string indexText(a.index.begin(), a.index.end());
    const auto entries = IndexEntries(indexText);
    if (entries.size() != count) return Fail(error, "index lists " + std::to_string(entries.size()) + " resources, header " + std::to_string(count));
    a.resources.clear();
    a.resources.reserve(count);
    size_t p = 0;
    for (uint32_t i = 0; i < count; ++i) {
        if (p + 26 > stream.size()) return Fail(error, "resource stream truncated");
        Resource r;
        r.typeId = U32(stream, p);
        const uint32_t size = U32(stream, p + 4);
        r.version = U16(stream, p + 8);
        for (int k = 0; k < 4; ++k) r.ram[k] = U32(stream, p + 10 + 4 * k);
        if (size < 26 || p + size > stream.size()) return Fail(error, "bad resource size");
        r.data.assign(stream.begin() + p + 26, stream.begin() + p + size);
        auto t = types.find(r.typeId);
        r.typeName = t == types.end() ? std::string() : t->second;
        r.sourceName = entries[i].second;
        if (r.typeName != entries[i].first) return Fail(error, "resource " + std::to_string(i) + " type does not match the index");
        a.resources.push_back(std::move(r));
        p += size;
    }
    if (p != stream.size()) return Fail(error, "bytes after the last resource");
    return true;
}

std::vector<uint8_t> Write(const Archive& a) {
    std::vector<uint8_t> stream;
    uint32_t ram[4] = {};
    for (const auto& r : a.resources) {
        Put32(stream, r.typeId);
        Put32(stream, static_cast<uint32_t>(r.data.size() + 26));
        Put16(stream, r.version);
        for (int k = 0; k < 4; ++k) {
            Put32(stream, r.ram[k]);
            ram[k] += r.ram[k];
        }
        stream.insert(stream.end(), r.data.begin(), r.data.end());
    }
    std::vector<uint8_t> out = a.header;
    out.insert(out.end(), a.typeTable.begin(), a.typeTable.end());
    const uint32_t blockOff = static_cast<uint32_t>(out.size());
    out.insert(out.end(), {'U', 'E', 'z', 'l'});
    Put32(out, a.alignment);
    out.push_back(4);
    const size_t chunk = a.alignment ? a.alignment : 0x4000;
    for (size_t p = 0; p < stream.size(); p += chunk) {
        const size_t n = stream.size() - p < chunk ? stream.size() - p : chunk;
        Put32(out, static_cast<uint32_t>(n));
        out.push_back(0);  // raw
        out.insert(out.end(), stream.begin() + p, stream.begin() + p + n);
    }
    Put32(out, 0);
    out.push_back(0);
    const uint32_t indexOff = static_cast<uint32_t>(out.size());
    out.insert(out.end(), a.index.begin(), a.index.end());

    Set32(out, 0x10, 0x48);
    Set32(out, 0x14, blockOff);
    Set32(out, 0x18, indexOff);
    for (int k = 0; k < 4; ++k) Set32(out, 0x1C + 4 * k, ram[k]);
    Set32(out, 0x40, static_cast<uint32_t>(a.resources.size()));
    Set32(out, 0x0C, Fnv1_32(out.data(), 12));
    Set32(out, 0x44, Fnv1_32(out.data() + 0x10, 0x34));
    return out;
}

}  // namespace mc::game::sds
