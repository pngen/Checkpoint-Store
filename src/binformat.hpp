#ifndef CHECKPOINTSTORE_BINFORMAT_HPP
#define CHECKPOINTSTORE_BINFORMAT_HPP

#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/identity/generations.hpp>
#include <checkpointstore/identity/identities.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace checkpointstore {

// A bounded, deterministic binary writer. All integer encodings are
// little-endian and fixed-width; strings and byte vectors carry length
// prefixes. This yields a canonical encoding for persistence and for the
// protocol so the same bytes always serialize to the same representation.
class BinWriter {
public:
    void u8(std::uint8_t v);
    void u16(std::uint16_t v);
    void u32(std::uint32_t v);
    void u64(std::uint64_t v);
    void i64(std::int64_t v);
    void boolean(bool v);
    void bytes(ByteView v);
    void string(std::string_view v);
    // Fixed-size array (array of uint8_t).
    void bytes_fixed(ByteView v);

    template <typename T>
    void id(const BasicId<T>& v) {
        u64(v.value());
    }
    template <typename T>
    void generation(const BasicGeneration<T>& v) {
        u64(v.value());
    }
    void sha256(const crypto::Sha256Digest& v);

    // UTC epoch milliseconds for time points (deterministic).
    void time_point(std::chrono::system_clock::time_point tp);

    [[nodiscard]] Bytes data() const { return out_; }
    [[nodiscard]] std::size_t size() const noexcept { return out_.size(); }

private:
    Bytes out_;
};

// A bounded binary reader with explicit bounds checking. Any read past the
// end throws a truncated/malformed error rather than reading out of bounds.
class BinReader {
public:
    explicit BinReader(ByteView view);

    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    std::uint64_t u64();
    std::int64_t i64();
    bool boolean();
    [[nodiscard]] ByteView bytes();      // length-prefixed view
    [[nodiscard]] std::string string();  // length-prefixed string
    [[nodiscard]] ByteView bytes_fixed(std::size_t n);

    template <typename T>
    BasicId<T> id() {
        return BasicId<T>(u64());
    }
    template <typename T>
    BasicGeneration<T> generation() {
        return BasicGeneration<T>(u64());
    }
    crypto::Sha256Digest sha256();

    std::chrono::system_clock::time_point time_point();

    [[nodiscard]] std::size_t remaining() const noexcept { return view_.size() - pos_; }
    [[nodiscard]] std::size_t position() const noexcept { return pos_; }
    void skip(std::size_t n);

private:
    void require(std::size_t n) const;

    ByteView view_;
    std::size_t pos_{0};
};

// Length-prefix encoding for a vector/size (with a hard bound against
// allocation bombs).
[[nodiscard]] std::uint32_t encode_count(std::size_t n);
[[nodiscard]] std::size_t decode_count(std::uint32_t n, std::size_t max);

}  // namespace checkpointstore

#endif
