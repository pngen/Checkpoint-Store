#ifndef CHECKPOINTSTORE_BASE_BYTE_HPP
#define CHECKPOINTSTORE_BASE_BYTE_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace checkpointstore {

using Byte = std::byte;
using Bytes = std::vector<std::byte>;
using ByteView = std::span<const std::byte>;

// Constructs a Bytes vector from a byte pointer and length.
[[nodiscard]] Bytes make_bytes(const void* data, std::size_t size);

// Constructs a Bytes vector from a string view (copies the characters).
[[nodiscard]] Bytes make_bytes(std::string_view text);

// Renders bytes as a lower-case hexadecimal string.
[[nodiscard]] std::string hex_string(ByteView bytes);

// Renders bytes as a lower-case hexadecimal string.
[[nodiscard]] std::string hex_string(const std::uint8_t* data, std::size_t size);

// Parses a hex string into bytes. Returns false on malformed input (odd
// length or non-hexadecimal characters).
[[nodiscard]] bool parse_hex(std::string_view text, Bytes& out);

// Compares two byte spans for content equality byte by byte.
[[nodiscard]] bool bytes_equal(ByteView a, ByteView b);

}  // namespace checkpointstore

#endif
