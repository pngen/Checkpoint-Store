#ifndef CHECKPOINTSTORE_STORAGE_DEDUP_HPP
#define CHECKPOINTSTORE_STORAGE_DEDUP_HPP

#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/identity/generations.hpp>
#include <checkpointstore/identity/identities.hpp>
#include <checkpointstore/model.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace checkpointstore {

// A deterministic hasher for SHA-256 digests used as unordered_map keys. The
// standard library does not provide a hash for std::array, and defining a
// std::hash specialization for an array type would clash with the standard
// library's own specialization, so we provide our own functor.
struct DigestHash {
    std::size_t operator()(const crypto::Sha256Digest& d) const noexcept {
        std::size_t h = 0;
        for (int i = 0; i < 8; ++i) {
            h = (h << 8) | static_cast<std::size_t>(d[static_cast<std::size_t>(i)]);
        }
        return h;
    }
};

// Deduplication is central to Checkpoint Store. A DedupTable tracks the
// logical->physical reference relationship for content-addressed blobs.
//
// The authoritative byte identity is the SHA-256 digest of the physical blob.
// A digest collision must never silently merge incompatible metadata: a blob
// is only shared when its digest AND its physical size agree with an existing
// verified blob. Refcounts are exact and can never underflow; duplicate
// release is rejected.
struct DedupEntry {
    BlobId blob_id;
    crypto::Sha256Digest digest;
    std::uint64_t physical_size = 0;
    std::uint64_t refcount = 0;       // number of logical chunk references
    IntegrityState integrity = IntegrityState::kUnknown;
    BlobGeneration generation;
};

// A deduplicated reference decision for one chunk.
enum class DedupResult { kMiss, kHit, kReject };

struct DedupLookup {
    DedupResult result = DedupResult::kMiss;
    BlobId blob_id;
    std::uint64_t physical_bytes_written = 0;
    std::uint64_t dedup_hits = 0;
    std::uint64_t dedup_misses = 0;
};

// An in-memory deduplication table guarded by an external mutex owned by the
// store. Exact accounting: logical bytes vs unique physical bytes, and the
// number of blob references.
class DedupTable {
public:
    // Looks up a digest with an expected physical size. A hit requires an
    // existing blob with the same digest and the same physical size that is
    // already in a non-corrupt state. Otherwise the lookup is a miss (or a
    // reject if a matching digest exists but sizes disagree).
    [[nodiscard]] DedupLookup lookup(const crypto::Sha256Digest& digest,
                                     std::uint64_t physical_size);

    // Records a new blob (dedup miss) with a refcount of one.
    // Returns false if a blob with the same digest and size already exists.
    [[nodiscard]] bool insert(BlobId id, const crypto::Sha256Digest& digest,
                              std::uint64_t physical_size, IntegrityState integrity,
                              BlobGeneration generation);

    // Adds one reference to an existing blob. Returns false if the blob is
    // unknown or if its integrity is CORRUPT (a corrupt blob must never be
    // silently shared).
    [[nodiscard]] bool add_reference(const crypto::Sha256Digest& digest);

    // Releases one reference. Returns false if the blob is unknown or if the
    // refcount would underflow.
    [[nodiscard]] bool release_reference(const crypto::Sha256Digest& digest);

    [[nodiscard]] std::uint64_t refcount(const crypto::Sha256Digest& digest) const;
    [[nodiscard]] bool contains(const crypto::Sha256Digest& digest) const;
    [[nodiscard]] const DedupEntry* find(const crypto::Sha256Digest& digest) const;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint64_t unique_physical_bytes() const;

    // A deterministic snapshot of all entries (for persistence and reporting).
    [[nodiscard]] std::vector<std::pair<crypto::Sha256Digest, DedupEntry>> snapshot() const;

    // Inserts a fully formed entry, used during state recovery after all
    // binding checks have been performed by the persistence layer.
    void insert_blob_entry_direct(const DedupEntry& e);

    // Applies a release from retirement/GC when a blob is no longer
    // referenced; returns true and drops the entry when the refcount reaches 0.
    [[nodiscard]] bool release_until_orphan(const crypto::Sha256Digest& digest);

private:
    std::unordered_map<crypto::Sha256Digest, DedupEntry, DigestHash> entries_;
};

// Computes dedup accounting summary: logical bytes, unique physical bytes,
// deduplicated bytes.
struct DedupAccounting {
    std::uint64_t logical_bytes = 0;
    std::uint64_t unique_physical_bytes = 0;
    std::uint64_t deduplicated_bytes = 0;
};

}  // namespace checkpointstore

#endif
