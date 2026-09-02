#ifndef CHECKPOINTSTORE_STORAGE_CHUNKING_HPP
#define CHECKPOINTSTORE_STORAGE_CHUNKING_HPP

#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/model.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace checkpointstore {

// Deterministic fixed-size chunking. The same input bytes and the same chunk
// size always produce the same chunk boundaries. Used as the 1.0.0 chunking
// strategy.
struct FixedChunker {
    std::uint64_t chunk_size = 0;

    // Validates chunk size, rejecting zero and overflowing configs.
    [[nodiscard]] bool is_valid() const noexcept;
    // Validates that a configured chunk size is acceptable.
    [[nodiscard]] static bool valid_chunk_size(std::uint64_t chunk_size) noexcept;
};

// Produces the byte ranges for a fixed-size split of a logical byte stream.
// The final piece is the tail remainder. Offsets are monotonic and
// non-overlapping, and the total of the ranges equals the input size.
struct ChunkedRange {
    std::uint64_t logical_offset = 0;
    std::uint64_t logical_size = 0;
};

[[nodiscard]] std::vector<ChunkedRange> fixed_chunk_ranges(std::uint64_t logical_size,
                                                           std::uint64_t chunk_size);

// The result of chunking an in-memory byte stream.
struct ChunkSlice {
    ChunkedRange range;
    Bytes bytes;
    crypto::Sha256Digest digest{};
};

// Chunks a byte stream into fixed-size slices and computes each slice's
// content address. Rejects an invalid chunk size.
[[nodiscard]] std::vector<ChunkSlice> chunk_stream(ByteView data, std::uint64_t chunk_size);

}  // namespace checkpointstore

#endif
