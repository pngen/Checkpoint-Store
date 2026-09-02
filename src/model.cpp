#include <checkpointstore/model.hpp>

namespace checkpointstore {

const char* to_string(CheckpointKind kind) noexcept {
    switch (kind) {
        case CheckpointKind::kFull: return "FULL";
        case CheckpointKind::kIncremental: return "INCREMENTAL";
        case CheckpointKind::kDifferential: return "DIFFERENTIAL";
        case CheckpointKind::kSharded: return "SHARDED";
        case CheckpointKind::kSynthetic: return "SYNTHETIC";
    }
    return "UNKNOWN";
}

const char* to_string(CheckpointLifecycle state) noexcept {
    switch (state) {
        case CheckpointLifecycle::kCreating: return "CREATING";
        case CheckpointLifecycle::kWriting: return "WRITING";
        case CheckpointLifecycle::kVerifying: return "VERIFYING";
        case CheckpointLifecycle::kCommitted: return "COMMITTED";
        case CheckpointLifecycle::kDegraded: return "DEGRADED";
        case CheckpointLifecycle::kStale: return "STALE";
        case CheckpointLifecycle::kInvalidated: return "INVALIDATED";
        case CheckpointLifecycle::kRestoring: return "RESTORING";
        case CheckpointLifecycle::kRetired: return "RETIRED";
        case CheckpointLifecycle::kGcEligible: return "GC_ELIGIBLE";
        case CheckpointLifecycle::kDeleted: return "DELETED";
        case CheckpointLifecycle::kFailed: return "FAILED";
    }
    return "UNKNOWN";
}

const char* to_string(IntegrityState state) noexcept {
    switch (state) {
        case IntegrityState::kUnknown: return "UNKNOWN";
        case IntegrityState::kUnverified: return "UNVERIFIED";
        case IntegrityState::kVerified: return "VERIFIED";
        case IntegrityState::kCorrupt: return "CORRUPT";
        case IntegrityState::kMissing: return "MISSING";
    }
    return "UNKNOWN";
}

const char* to_string(Provenance p) noexcept {
    switch (p) {
        case Provenance::kMeasured: return "MEASURED";
        case Provenance::kReported: return "REPORTED";
        case Provenance::kDerived: return "DERIVED";
        case Provenance::kSynthetic: return "SYNTHETIC";
        case Provenance::kUnknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* to_string(StorageTierClass tier) noexcept {
    switch (tier) {
        case StorageTierClass::kLocalFilesystem: return "LOCAL_FILESYSTEM";
        case StorageTierClass::kLocalNvmeClass: return "LOCAL_NVME_CLASS";
        case StorageTierClass::kSharedFilesystemClass: return "SHARED_FILESYSTEM_CLASS";
        case StorageTierClass::kObjectStorageClass: return "OBJECT_STORAGE_CLASS";
        case StorageTierClass::kRemoteBlockClass: return "REMOTE_BLOCK_CLASS";
        case StorageTierClass::kArchivalClass: return "ARCHIVAL_CLASS";
        case StorageTierClass::kSyntheticRemote: return "SYNTHETIC_REMOTE";
        case StorageTierClass::kUnknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* to_string(ReplicaDurabilityState state) noexcept {
    switch (state) {
        case ReplicaDurabilityState::kUnderReplicated: return "UNDER_REPLICATED";
        case ReplicaDurabilityState::kHealthy: return "HEALTHY";
        case ReplicaDurabilityState::kDegraded: return "DEGRADED";
        case ReplicaDurabilityState::kRebuilding: return "REBUILDING";
        case ReplicaDurabilityState::kFailed: return "FAILED";
    }
    return "UNKNOWN";
}

const char* to_string(RestorePriority priority) noexcept {
    switch (priority) {
        case RestorePriority::kCritical: return "CRITICAL";
        case RestorePriority::kHigh: return "HIGH";
        case RestorePriority::kNormal: return "NORMAL";
        case RestorePriority::kLow: return "LOW";
        case RestorePriority::kBackground: return "BACKGROUND";
    }
    return "UNKNOWN";
}

const char* to_string(RetentionClass c) noexcept {
    switch (c) {
        case RetentionClass::kPinned: return "PINNED";
        case RetentionClass::kKeepLatestN: return "KEEP_LATEST_N";
        case RetentionClass::kTtl: return "TTL";
        case RetentionClass::kAncestryRequired: return "ANCESTRY_REQUIRED";
        case RetentionClass::kRestorePoint: return "RESTORE_POINT";
        case RetentionClass::kRecomputable: return "RECOMPUTABLE";
        case RetentionClass::kGcEligible: return "GC_ELIGIBLE";
    }
    return "UNKNOWN";
}

const char* to_string(Freshness f) noexcept {
    switch (f) {
        case Freshness::kCurrent: return "CURRENT";
        case Freshness::kStale: return "STALE";
        case Freshness::kRevalidationRequired: return "REVALIDATION_REQUIRED";
        case Freshness::kUnknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* to_string(DurabilityClass d) noexcept {
    switch (d) {
        case DurabilityClass::kUnknown: return "UNKNOWN";
        case DurabilityClass::kEphemeral: return "EPHEMERAL";
        case DurabilityClass::kLocal: return "LOCAL";
        case DurabilityClass::kReplicated: return "REPLICATED";
        case DurabilityClass::kArchival: return "ARCHIVAL";
    }
    return "UNKNOWN";
}

const char* to_string(ReplicaSourceKind k) noexcept {
    switch (k) {
        case ReplicaSourceKind::kLocal: return "LOCAL";
        case ReplicaSourceKind::kSyntheticRemote: return "SYNTHETIC_REMOTE";
        case ReplicaSourceKind::kSyntheticObject: return "SYNTHETIC_OBJECT";
        case ReplicaSourceKind::kSyntheticArchive: return "SYNTHETIC_ARCHIVE";
    }
    return "UNKNOWN";
}

const char* to_string(ReplicaRole role) noexcept {
    switch (role) {
        case ReplicaRole::kAuthoritative: return "AUTHORITATIVE";
        case ReplicaRole::kSecondary: return "SECONDARY";
        case ReplicaRole::kCandidate: return "CANDIDATE";
        case ReplicaRole::kQuarantined: return "QUARANTINED";
    }
    return "UNKNOWN";
}

const char* to_string(RestoreSourceResult r) noexcept {
    switch (r) {
        case RestoreSourceResult::kSuccess: return "SUCCESS";
        case RestoreSourceResult::kCorrupt: return "CORRUPT";
        case RestoreSourceResult::kMissing: return "MISSING";
        case RestoreSourceResult::kUnavailable: return "UNAVAILABLE";
        case RestoreSourceResult::kFallback: return "FALLBACK";
    }
    return "UNKNOWN";
}

const char* to_string(GcPhase p) noexcept {
    switch (p) {
        case GcPhase::kIdle: return "IDLE";
        case GcPhase::kSnapshotting: return "SNAPSHOTTING";
        case GcPhase::kMarking: return "MARKING";
        case GcPhase::kSweeping: return "SWEEPING";
        case GcPhase::kComplete: return "COMPLETE";
        case GcPhase::kFailed: return "FAILED";
    }
    return "UNKNOWN";
}

crypto::Sha256Digest content_address(ByteView data) {
    return crypto::sha256(data);
}

BlobId blob_id_from_digest(const crypto::Sha256Digest& digest) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<std::uint64_t>(digest[static_cast<std::size_t>(i)]);
    }
    // Ensure the null sentinel is never derived from content.
    if (value == 0) {
        value = 1;
    }
    return BlobId(value);
}

ChunkId chunk_id_from_digest(const crypto::Sha256Digest& digest) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value = (value << 8) | static_cast<std::uint64_t>(digest[static_cast<std::size_t>(i)]);
    }
    if (value == 0) {
        value = 1;
    }
    return ChunkId(value);
}

}  // namespace checkpointstore
