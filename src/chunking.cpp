#include <checkpointstore/storage/chunking.hpp>

#include <checkpointstore/base/error.hpp>

namespace checkpointstore {

bool FixedChunker::is_valid() const noexcept {
    return valid_chunk_size(chunk_size);
}

bool FixedChunker::valid_chunk_size(std::uint64_t chunk_size) noexcept {
    // Chunk size must be non-zero and bounded to keep per-chunk allocations
    // bounded. The upper bound is generous but finite (64 MiB) so that chunk
    // metadata and per-chunk buffers cannot be driven arbitrarily large.
    constexpr std::uint64_t kMinChunkSize = 1;
    constexpr std::uint64_t kMaxChunkSize = 64u * 1024u * 1024u;
    if (chunk_size < kMinChunkSize) {
        return false;
    }
    if (chunk_size > kMaxChunkSize) {
        return false;
    }
    return true;
}

std::vector<ChunkedRange> fixed_chunk_ranges(std::uint64_t logical_size,
                                             std::uint64_t chunk_size) {
    if (!FixedChunker::valid_chunk_size(chunk_size)) {
        throw_error(ErrorCode::kInvalidArgument, "invalid chunk size");
    }
    std::vector<ChunkedRange> ranges;
    if (logical_size == 0) {
        return ranges;
    }
    std::uint64_t offset = 0;
    while (offset < logical_size) {
        const std::uint64_t remaining = logical_size - offset;
        const std::uint64_t take = (remaining < chunk_size) ? remaining : chunk_size;
        ranges.push_back(ChunkedRange{offset, take});
        offset += take;
    }
    return ranges;
}

std::vector<ChunkSlice> chunk_stream(ByteView data, std::uint64_t chunk_size) {
    if (!FixedChunker::valid_chunk_size(chunk_size)) {
        throw_error(ErrorCode::kInvalidArgument, "invalid chunk size");
    }
    std::vector<ChunkSlice> slices;
    std::size_t offset = 0;
    const auto* base = data.data();
    while (offset < data.size()) {
        const std::size_t remaining = data.size() - offset;
        const std::size_t take = (remaining < static_cast<std::size_t>(chunk_size))
                                     ? remaining
                                     : static_cast<std::size_t>(chunk_size);
        Bytes piece(base + offset, base + offset + take);
        auto digest = crypto::sha256(ByteView(piece.data(), piece.size()));
        slices.push_back(ChunkSlice{ChunkedRange{static_cast<std::uint64_t>(offset),
                                                 static_cast<std::uint64_t>(take)},
                                    std::move(piece), digest});
        offset += take;
    }
    return slices;
}

}  // namespace checkpointstore
