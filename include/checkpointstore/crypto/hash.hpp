#ifndef CHECKPOINTSTORE_CRYPTO_HASH_HPP
#define CHECKPOINTSTORE_CRYPTO_HASH_HPP

#include <checkpointstore/base/byte.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace checkpointstore {
namespace crypto {

// SHA-256 digest is 32 bytes. This is the cryptographic content identity used
// for chunk content, manifest semantics, and checkpoint content.
using Sha256Digest = std::array<std::uint8_t, 32>;

using Crc32 = std::uint32_t;

// Streaming SHA-256. Feed the full content through update() then call final().
class Sha256Hasher {
public:
    Sha256Hasher() noexcept;
    void update(ByteView data);
    [[nodiscard]] Sha256Digest final();
    // One-shot helper.
    [[nodiscard]] static Sha256Digest compute(ByteView data);

private:
    void transform(const std::uint8_t* block);
    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_;
    std::uint64_t total_len_;
    std::size_t buffered_;
};

// Convenience one-shot SHA-256 over a byte span.
[[nodiscard]] Sha256Digest sha256(ByteView data);

// Convenience one-shot SHA-256 over a string view.
[[nodiscard]] Sha256Digest sha256(std::string_view data);

// CRC-32 (IEEE 802.3) over a byte span. The seed parameter enables chaining.
// Used for metadata framing and persistence framing corruption detection.
[[nodiscard]] Crc32 crc32(ByteView data, Crc32 seed = 0);

// Renders a SHA-256 digest as a lower-case hexadecimal string (64 chars).
[[nodiscard]] std::string hex(const Sha256Digest& digest);

// Compares two digests for equality.
[[nodiscard]] bool equal(const Sha256Digest& a, const Sha256Digest& b) noexcept;

}  // namespace crypto
}  // namespace checkpointstore

#endif
