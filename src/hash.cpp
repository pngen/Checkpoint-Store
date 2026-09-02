#include <checkpointstore/crypto/hash.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace checkpointstore {
namespace crypto {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

constexpr std::uint32_t rotr(std::uint32_t x, std::uint32_t n) {
    return (x >> n) | (x << (32u - n));
}

}  // namespace

Sha256Hasher::Sha256Hasher() noexcept
    : state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
             0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u},
      total_len_(0),
      buffered_(0) {
    buffer_.fill(0);
}

void Sha256Hasher::transform(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> m{};
    for (std::size_t i = 0; i < 16; ++i) {
        const std::size_t j = i * 4;
        m[i] = (static_cast<std::uint32_t>(block[j]) << 24) |
               (static_cast<std::uint32_t>(block[j + 1]) << 16) |
               (static_cast<std::uint32_t>(block[j + 2]) << 8) |
               (static_cast<std::uint32_t>(block[j + 3]));
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        const std::uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t temp1 = h + s1 + ch + kRoundConstants[i] + m[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256Hasher::update(ByteView data) {
    const auto* p = reinterpret_cast<const std::uint8_t*>(data.data());
    std::size_t len = data.size();
    total_len_ += static_cast<std::uint64_t>(len);

    if (buffered_ > 0) {
        const std::size_t take = std::min(std::size_t{64} - buffered_, len);
        std::memcpy(buffer_.data() + buffered_, p, take);
        buffered_ += take;
        p += take;
        len -= take;
        if (buffered_ == 64) {
            transform(buffer_.data());
            buffered_ = 0;
        }
    }
    while (len >= 64) {
        transform(p);
        p += 64;
        len -= 64;
    }
    if (len > 0) {
        std::memcpy(buffer_.data(), p, len);
        buffered_ = len;
    }
}

Sha256Digest Sha256Hasher::final() {
    const std::uint64_t bit_len = total_len_ * 8u;
    std::array<std::uint8_t, 8> len_bytes{};
    len_bytes[0] = static_cast<std::uint8_t>(bit_len >> 56);
    len_bytes[1] = static_cast<std::uint8_t>(bit_len >> 48);
    len_bytes[2] = static_cast<std::uint8_t>(bit_len >> 40);
    len_bytes[3] = static_cast<std::uint8_t>(bit_len >> 32);
    len_bytes[4] = static_cast<std::uint8_t>(bit_len >> 24);
    len_bytes[5] = static_cast<std::uint8_t>(bit_len >> 16);
    len_bytes[6] = static_cast<std::uint8_t>(bit_len >> 8);
    len_bytes[7] = static_cast<std::uint8_t>(bit_len);

    const std::uint8_t padding = 0x80;
    update(ByteView(reinterpret_cast<const Byte*>(&padding), 1));
    const std::uint8_t zero = 0x00;
    while (buffered_ != 56) {
        update(ByteView(reinterpret_cast<const Byte*>(&zero), 1));
    }
    update(ByteView(reinterpret_cast<const Byte*>(len_bytes.data()), len_bytes.size()));

    Sha256Digest result{};
    for (std::size_t i = 0; i < 8; ++i) {
        result[i * 4] = static_cast<std::uint8_t>(state_[i] >> 24);
        result[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
        result[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
        result[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
    }
    return result;
}

Sha256Digest Sha256Hasher::compute(ByteView data) {
    Sha256Hasher hasher;
    hasher.update(data);
    return hasher.final();
}

Sha256Digest sha256(ByteView data) { return Sha256Hasher::compute(data); }

Sha256Digest sha256(std::string_view data) {
    return Sha256Hasher::compute(ByteView(reinterpret_cast<const Byte*>(data.data()), data.size()));
}

Crc32 crc32(ByteView data, Crc32 seed) {
    Crc32 crc = seed ^ 0xFFFFFFFFu;
    for (Byte b : data) {
        crc ^= static_cast<std::uint8_t>(b);
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

std::string hex(const Sha256Digest& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.resize(64);
    for (std::size_t i = 0; i < 32; ++i) {
        out[i * 2] = kHex[digest[i] >> 4];
        out[i * 2 + 1] = kHex[digest[i] & 0x0F];
    }
    return out;
}

bool equal(const Sha256Digest& a, const Sha256Digest& b) noexcept {
    return a == b;
}

}  // namespace crypto

// --------------------------------------------------------------------------
// Byte helpers
// --------------------------------------------------------------------------
Bytes make_bytes(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    return Bytes(reinterpret_cast<const Byte*>(p), reinterpret_cast<const Byte*>(p) + size);
}

Bytes make_bytes(std::string_view text) {
    return Bytes(reinterpret_cast<const Byte*>(text.data()),
                 reinterpret_cast<const Byte*>(text.data()) + text.size());
}

std::string hex_string(ByteView bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (Byte b : bytes) {
        const auto v = static_cast<std::uint8_t>(b);
        out.push_back(kHex[v >> 4]);
        out.push_back(kHex[v & 0x0F]);
    }
    return out;
}

std::string hex_string(const std::uint8_t* data, std::size_t size) {
    return hex_string(ByteView(reinterpret_cast<const Byte*>(data), size));
}

bool parse_hex(std::string_view text, Bytes& out) {
    if ((text.size() % 2) != 0) {
        return false;
    }
    out.clear();
    out.reserve(text.size() / 2);
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = nibble(text[i]);
        const int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<Byte>((hi << 4) | lo));
    }
    return true;
}

bool bytes_equal(ByteView a, ByteView b) {
    if (a.size() != b.size()) {
        return false;
    }
    return std::equal(a.begin(), a.end(), b.begin());
}

}  // namespace checkpointstore
