#include <checkpointstore/storage/dedup.hpp>

#include <checkpointstore/base/error.hpp>

#include <limits>

namespace checkpointstore {

DedupLookup DedupTable::lookup(const crypto::Sha256Digest& digest, std::uint64_t physical_size) {
    DedupLookup result;
    if (physical_size == 0) {
        // Zero-length content is a valid, unique value but we still require a
        // physical write for observability; treat as a miss that reserves zero
        // bytes. It is never merged with other content.
        result.result = DedupResult::kMiss;
        result.physical_bytes_written = 0;
        return result;
    }
    auto it = entries_.find(digest);
    if (it == entries_.end()) {
        result.result = DedupResult::kMiss;
        return result;
    }
    const auto& e = it->second;
    if (e.physical_size != physical_size) {
        // Digest collision or incompatible size: never silently merge.
        result.result = DedupResult::kReject;
        result.blob_id = e.blob_id;
        throw_error(ErrorCode::kConflict,
                    "dedup digest matched but physical size disagrees");
    }
    if (e.integrity == IntegrityState::kCorrupt) {
        result.result = DedupResult::kReject;
        result.blob_id = e.blob_id;
        throw_error(ErrorCode::kCorrupt,
                    "dedup candidate is corrupt and will not be shared");
    }
    result.result = DedupResult::kHit;
    result.blob_id = e.blob_id;
    result.physical_bytes_written = 0;
    result.dedup_hits = 0;
    return result;
}

bool DedupTable::insert(BlobId id, const crypto::Sha256Digest& digest,
                        std::uint64_t physical_size, IntegrityState integrity,
                        BlobGeneration generation) {
    auto it = entries_.find(digest);
    if (it != entries_.end()) {
        if (it->second.physical_size == physical_size) {
            return false;  // already present with identical content
        }
        throw_error(ErrorCode::kConflict,
                    "dedup insert collision: identical digest, different size");
    }
    DedupEntry e;
    e.blob_id = id;
    e.digest = digest;
    e.physical_size = physical_size;
    e.refcount = 1;
    e.integrity = integrity;
    e.generation = generation;
    entries_.emplace(digest, e);
    return true;
}

bool DedupTable::add_reference(const crypto::Sha256Digest& digest) {
    auto it = entries_.find(digest);
    if (it == entries_.end()) {
        return false;
    }
    auto& e = it->second;
    if (e.integrity == IntegrityState::kCorrupt) {
        throw_error(ErrorCode::kCorrupt, "cannot reference a corrupt blob");
    }
    if (e.refcount == std::numeric_limits<std::uint64_t>::max()) {
        throw_error(ErrorCode::kOverflow, "refcount overflow");
    }
    ++e.refcount;
    return true;
}

bool DedupTable::release_reference(const crypto::Sha256Digest& digest) {
    auto it = entries_.find(digest);
    if (it == entries_.end()) {
        return false;
    }
    auto& e = it->second;
    if (e.refcount == 0) {
        return false;  // duplicate release must never underflow refcount
    }
    --e.refcount;
    return true;
}

std::uint64_t DedupTable::refcount(const crypto::Sha256Digest& digest) const {
    auto it = entries_.find(digest);
    if (it == entries_.end()) {
        return 0;
    }
    return it->second.refcount;
}

bool DedupTable::contains(const crypto::Sha256Digest& digest) const {
    return entries_.find(digest) != entries_.end();
}

const DedupEntry* DedupTable::find(const crypto::Sha256Digest& digest) const {
    auto it = entries_.find(digest);
    if (it == entries_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::size_t DedupTable::size() const noexcept {
    return entries_.size();
}

std::uint64_t DedupTable::unique_physical_bytes() const {
    std::uint64_t total = 0;
    for (const auto& [digest, e] : entries_) {
        (void)digest;
        total += e.physical_size;
    }
    return total;
}

std::vector<std::pair<crypto::Sha256Digest, DedupEntry>> DedupTable::snapshot() const {
    std::vector<std::pair<crypto::Sha256Digest, DedupEntry>> out;
    out.reserve(entries_.size());
    for (const auto& [digest, e] : entries_) {
        out.emplace_back(digest, e);
    }
    return out;
}

void DedupTable::insert_blob_entry_direct(const DedupEntry& e) {
    entries_[e.digest] = e;
}

bool DedupTable::release_until_orphan(const crypto::Sha256Digest& digest) {
    auto it = entries_.find(digest);
    if (it == entries_.end()) {
        return false;
    }
    if (it->second.refcount > 0) {
        return false;
    }
    entries_.erase(it);
    return true;
}

}  // namespace checkpointstore
