// Deflate decoder (RFC 1951) with canonical Huffman tables decoded symbol by symbol (count/symbol arrays, as the
// RFC's construction describes). Fast enough for the few MB of the gui archives.
#include "game/inflate.h"

#include <cstring>

namespace mc::game {
namespace {

struct Bits {
    const uint8_t* p;
    size_t n;
    size_t pos = 0;  // byte position
    uint32_t buf = 0;
    int cnt = 0;
    bool error = false;

    uint32_t Need(int k) {  // k <= 24
        while (cnt < k) {
            if (pos >= n) {
                error = true;
                return 0;
            }
            buf |= static_cast<uint32_t>(p[pos++]) << cnt;
            cnt += 8;
        }
        const uint32_t v = buf & ((1u << k) - 1);
        buf >>= k;
        cnt -= k;
        return v;
    }
    void AlignByte() {
        buf = 0;
        cnt = 0;
    }
};

constexpr int kMaxBits = 15;

struct Huffman {
    uint16_t count[kMaxBits + 1] = {};
    uint16_t symbol[320] = {};

    // False if the lengths are over-subscribed (an incomplete code is allowed, as zlib does for one-code trees).
    bool Build(const uint8_t* lengths, int n) {
        std::memset(count, 0, sizeof count);
        for (int i = 0; i < n; ++i) ++count[lengths[i]];
        count[0] = 0;
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) return false;
        }
        uint16_t offs[kMaxBits + 1];
        offs[1] = 0;
        for (int len = 1; len < kMaxBits; ++len) offs[len + 1] = static_cast<uint16_t>(offs[len] + count[len]);
        for (int i = 0; i < n; ++i)
            if (lengths[i]) symbol[offs[lengths[i]]++] = static_cast<uint16_t>(i);
        return true;
    }

    int Decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= static_cast<int>(b.Need(1));
            if (b.error) return -1;
            const int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

constexpr uint16_t kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool Codes(Bits& b, std::vector<uint8_t>& out, const Huffman& lit, const Huffman& dist, size_t outStart) {
    for (;;) {
        const int sym = lit.Decode(b);
        if (sym < 0) return false;
        if (sym < 256) {
            out.push_back(static_cast<uint8_t>(sym));
            continue;
        }
        if (sym == 256) return true;
        const int li = sym - 257;
        if (li >= 29) return false;
        const size_t len = kLenBase[li] + b.Need(kLenExtra[li]);
        const int ds = dist.Decode(b);
        if (ds < 0 || ds >= 30) return false;
        const size_t d = kDistBase[ds] + b.Need(kDistExtra[ds]);
        if (b.error || d > out.size() - outStart) return false;
        const size_t from = out.size() - d;
        for (size_t i = 0; i < len; ++i) out.push_back(out[from + i]);
    }
}

}  // namespace

uint32_t Adler32(const uint8_t* p, size_t n, uint32_t adler) {
    uint32_t a = adler & 0xFFFF, b = adler >> 16;
    while (n) {
        size_t k = n < 5552 ? n : 5552;
        n -= k;
        while (k--) {
            a += *p++;
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    return (b << 16) | a;
}

bool Inflate(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t* used) {
    Bits b{in, inSize};
    const size_t outStart = out.size();
    int last = 0;
    do {
        last = static_cast<int>(b.Need(1));
        const uint32_t type = b.Need(2);
        if (b.error) return false;
        if (type == 0) {  // stored
            b.AlignByte();
            if (b.pos + 4 > inSize) return false;
            const uint32_t len = in[b.pos] | (in[b.pos + 1] << 8);
            const uint32_t nlen = in[b.pos + 2] | (in[b.pos + 3] << 8);
            b.pos += 4;
            if ((len ^ 0xFFFF) != nlen || b.pos + len > inSize) return false;
            out.insert(out.end(), in + b.pos, in + b.pos + len);
            b.pos += len;
        } else if (type == 1) {  // fixed codes
            static Huffman lit, dist;
            static bool built = false;
            if (!built) {
                uint8_t l[288];
                int i = 0;
                for (; i < 144; ++i) l[i] = 8;
                for (; i < 256; ++i) l[i] = 9;
                for (; i < 280; ++i) l[i] = 7;
                for (; i < 288; ++i) l[i] = 8;
                lit.Build(l, 288);
                uint8_t d[30];
                for (auto& x : d) x = 5;
                dist.Build(d, 30);
                built = true;
            }
            if (!Codes(b, out, lit, dist, outStart)) return false;
        } else if (type == 2) {  // dynamic codes
            const int nlen = static_cast<int>(b.Need(5)) + 257;
            const int ndist = static_cast<int>(b.Need(5)) + 1;
            const int ncode = static_cast<int>(b.Need(4)) + 4;
            if (b.error || nlen > 286 || ndist > 30) return false;
            static constexpr uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            uint8_t lengths[320] = {};
            for (int i = 0; i < ncode; ++i) lengths[kOrder[i]] = static_cast<uint8_t>(b.Need(3));
            Huffman lencode;
            if (b.error || !lencode.Build(lengths, 19)) return false;
            int index = 0;
            while (index < nlen + ndist) {
                int sym = lencode.Decode(b);
                if (sym < 0) return false;
                if (sym < 16) {
                    lengths[index++] = static_cast<uint8_t>(sym);
                    continue;
                }
                uint8_t len = 0;
                int rep = 0;
                if (sym == 16) {
                    if (index == 0) return false;
                    len = lengths[index - 1];
                    rep = 3 + static_cast<int>(b.Need(2));
                } else if (sym == 17) {
                    rep = 3 + static_cast<int>(b.Need(3));
                } else {
                    rep = 11 + static_cast<int>(b.Need(7));
                }
                if (b.error || index + rep > nlen + ndist) return false;
                while (rep--) lengths[index++] = len;
            }
            if (lengths[256] == 0) return false;
            Huffman lit, dist;
            if (!lit.Build(lengths, nlen) || !dist.Build(lengths + nlen, ndist)) return false;
            if (!Codes(b, out, lit, dist, outStart)) return false;
        } else {
            return false;
        }
        if (b.error) return false;
    } while (!last);
    if (used) *used = b.pos;
    return true;
}

bool ZlibInflate(const uint8_t* in, size_t inSize, std::vector<uint8_t>& out, size_t* used) {
    if (inSize < 6) return false;
    const uint8_t cmf = in[0], flg = in[1];
    if ((cmf & 0x0F) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) return false;
    const size_t start = out.size();
    size_t n = 0;
    if (!Inflate(in + 2, inSize - 2, out, &n)) return false;
    const size_t at = 2 + n;
    if (at + 4 > inSize) return false;
    const uint32_t want = (static_cast<uint32_t>(in[at]) << 24) | (in[at + 1] << 16) | (in[at + 2] << 8) | in[at + 3];
    if (Adler32(out.data() + start, out.size() - start) != want) return false;
    if (used) *used = at + 4;
    return true;
}

}  // namespace mc::game
