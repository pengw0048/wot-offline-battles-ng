#pragma once
// Python 2.7 identical round draws for the Bot line-up and the round start: planner.stable_seed
// (sha1), random.Random seeded from a 32-bit key, and the 32-bit #1513 str.__hash__ that seeds
// random.Random(text).
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace native_battle {
namespace bots {
namespace random27 {
inline std::uint32_t rotl(std::uint32_t x, unsigned n) { return (x << n) | (x >> (32 - n)); }

// planner.stable_seed: int(sha1(payload).hexdigest()[:8], 16) & 0x7fffffff.
inline std::uint32_t stableSeed(const std::string& payload) {
    std::vector<std::uint8_t> bytes(payload.begin(), payload.end());
    const std::uint64_t bits = bytes.size() * 8ull;
    bytes.push_back(0x80);
    while (bytes.size() % 64 != 56) bytes.push_back(0);
    for (int i = 7; i >= 0; --i) bytes.push_back(std::uint8_t(bits >> (8 * i)));
    std::uint32_t h0 = 0x67452301u, h1 = 0xefcdab89u, h2 = 0x98badcfeu, h3 = 0x10325476u, h4 = 0xc3d2e1f0u;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 64) {
        std::uint32_t w[80]{};
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 4; ++j) w[i] = (w[i] << 8) | bytes[offset + 4 * i + j];
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = 0, k = 0;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5a827999u; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1u; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdcu; }
            else { f = b ^ c ^ d; k = 0xca62c1d6u; }
            const auto tmp = rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rotl(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }
    return h0 & 0x7fffffffu;
}

// hash(str) of the 32-bit #1513 interpreter (no hash randomization), as unsigned long.
inline std::uint32_t stringHash(const std::string& text) {
    if (text.empty()) return 0u;
    std::uint32_t x = std::uint32_t(static_cast<unsigned char>(text[0])) << 7;
    for (unsigned char c : text) x = (1000003u * x) ^ c;
    x ^= std::uint32_t(text.size());
    if (x == 0xffffffffu) x = 0xfffffffeu;
    return x;
}

// random.Random(seed) for one key word (a 31-bit int seed or a hashed text), with Python 2.7's
// random(), uniform(), randrange(n), choice() and shuffle().
class Random {
    std::uint32_t state_[624]{};
    int index_ = 624;
    std::uint32_t next() {
        if (index_ >= 624) {
            for (int i = 0; i < 624; ++i) {
                const auto y = (state_[i] & 0x80000000u) | (state_[(i + 1) % 624] & 0x7fffffffu);
                state_[i] = state_[(i + 397) % 624] ^ (y >> 1) ^ ((y & 1) ? 0x9908b0dfu : 0u);
            }
            index_ = 0;
        }
        auto y = state_[index_++];
        y ^= y >> 11; y ^= (y << 7) & 0x9d2c5680u; y ^= (y << 15) & 0xefc60000u; y ^= y >> 18;
        return y;
    }
public:
    explicit Random(std::uint32_t key) {
        state_[0] = 19650218u;
        for (int i = 1; i < 624; ++i) state_[i] = 1812433253u * (state_[i - 1] ^ (state_[i - 1] >> 30)) + i;
        int i = 1;
        for (int k = 624; k > 0; --k) {
            state_[i] = (state_[i] ^ ((state_[i - 1] ^ (state_[i - 1] >> 30)) * 1664525u)) + key;
            if (++i >= 624) { state_[0] = state_[623]; i = 1; }
        }
        for (int k = 623; k > 0; --k) {
            state_[i] = (state_[i] ^ ((state_[i - 1] ^ (state_[i - 1] >> 30)) * 1566083941u)) - i;
            if (++i >= 624) { state_[0] = state_[623]; i = 1; }
        }
        state_[0] = 0x80000000u;
    }
    // random.Random('text'): the key is the text's hash.
    explicit Random(const std::string& text) : Random(stringHash(text)) {}
    double random() { const auto a = next() >> 5, b = next() >> 6; return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0); }
    double uniform(double a, double b) { return a + (b - a) * random(); }
    std::size_t randrange(std::size_t n) { return std::size_t(random() * double(n)); }
    template<class T> const T& choice(const std::vector<T>& values) { return values[randrange(values.size())]; }
    template<class T> void shuffle(std::vector<T>& values) {
        for (std::size_t i = values.size(); i-- > 1;) std::swap(values[i], values[std::size_t(random() * double(i + 1))]);
    }
};
}
}
}
