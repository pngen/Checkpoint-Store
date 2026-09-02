#include "test_util.hpp"
#include <checkpointstore/storage/dedup.hpp>
#include <checkpointstore/base/error.hpp>

using namespace checkpointstore;

int main() {
    DedupTable dt;
    auto d1 = crypto::sha256(std::string_view("alpha"));
    // Insert a blob.
    CHECK(dt.insert(BlobId(1), d1, 5, IntegrityState::kVerified, BlobGeneration(1)));
    CHECK(!dt.insert(BlobId(2), d1, 5, IntegrityState::kVerified, BlobGeneration(1))); // duplicate content

    // Lookup hit.
    auto h = dt.lookup(d1, 5);
    CHECK(h.result == DedupResult::kHit);
    CHECK_EQ(h.blob_id.value(), 1u);

    // Size mismatch must never silently merge.
    bool threw = false;
    try { (void)dt.lookup(d1, 6); } catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kConflict; }
    CHECK(threw);

    // Refcount exact, never negative, duplicate release rejected.
    CHECK(dt.add_reference(d1));
    CHECK_EQ(dt.refcount(d1), 2u);
    CHECK(dt.release_reference(d1));
    CHECK_EQ(dt.refcount(d1), 1u);
    CHECK(dt.release_reference(d1));
    CHECK_EQ(dt.refcount(d1), 0u);
    CHECK(!dt.release_reference(d1));  // underflow rejected -> returns false

    // Corrupt blob cannot be shared.
    auto d2 = crypto::sha256(std::string_view("beta"));
    (void)dt.insert(BlobId(5), d2, 4, IntegrityState::kCorrupt, BlobGeneration(1));
    threw = false;
    try { (void)dt.lookup(d2, 4); } catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kCorrupt; }
    CHECK(threw);

    // Dedup reduces physical bytes.
    auto d3 = crypto::sha256(std::string_view("gamma"));
    CHECK(dt.insert(BlobId(9), d3, 7, IntegrityState::kVerified, BlobGeneration(1)));
    CHECK_EQ(dt.unique_physical_bytes(), 5u + 4u + 7u);

    // Snapshot.
    auto snap = dt.snapshot();
    CHECK_EQ(snap.size(), 3u);

    return cpstest::finish("test_dedup");
}
