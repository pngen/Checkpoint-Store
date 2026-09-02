#include "test_util.hpp"
#include <checkpointstore/storage/chunking.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>

using namespace checkpointstore;

int main() {
    // Deterministic ranges.
    auto ranges = fixed_chunk_ranges(100, 32);
    CHECK_EQ(ranges.size(), 4u);
    CHECK(ranges[0].logical_offset == 0 && ranges[0].logical_size == 32);
    CHECK(ranges[3].logical_offset == 96 && ranges[3].logical_size == 4);
    std::uint64_t sum = 0;
    for (auto& r : ranges) sum += r.logical_size;
    CHECK_EQ(sum, 100u);

    // Empty input produces no chunks.
    CHECK(fixed_chunk_ranges(0, 32).empty());

    // Invalid chunk sizes rejected.
    CHECK(!FixedChunker::valid_chunk_size(0));
    CHECK(!FixedChunker::valid_chunk_size(64 * 1024 * 1024 + 1));
    CHECK(FixedChunker::valid_chunk_size(4096));
    bool threw = false;
    try { (void)fixed_chunk_ranges(100, 0); } catch (const CheckpointStoreError& e) {
        threw = e.code() == ErrorCode::kInvalidArgument;
    }
    CHECK(threw);

    // chunk_stream produces deterministic boundaries and correct digests.
    Bytes data = make_bytes("hello world hello world hello world");
    auto slices = chunk_stream(ByteView(data.data(), data.size()), 8);
    CHECK_EQ(slices.size(), 5u);
    std::string joined;
    for (auto& s : slices) joined.append(reinterpret_cast<const char*>(s.bytes.data()), s.bytes.size());
    CHECK(joined == std::string(reinterpret_cast<const char*>(data.data()), data.size()));
    // Digest of a slice matches a direct hash.
    CHECK(crypto::equal(slices[0].digest, crypto::sha256(ByteView(slices[0].bytes.data(), slices[0].bytes.size()))));

    // No gaps, no overlaps, total equals input.
    std::uint64_t next = 0;
    for (auto& r : ranges) { CHECK(r.logical_offset == next); next += r.logical_size; }
    CHECK_EQ(next, 100u);

    return cpstest::finish("test_chunking");
}
