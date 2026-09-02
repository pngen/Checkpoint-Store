#ifndef CHECKPOINTSTORE_MODEL_HPP
#define CHECKPOINTSTORE_MODEL_HPP

#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/identity/generations.hpp>
#include <checkpointstore/identity/identities.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace checkpointstore {

// ==========================================================================
// Enumerations
// ==========================================================================

enum class CheckpointKind : std::uint8_t {
    kFull = 0,
    kIncremental,
    kDifferential,
    kSharded,
    kSynthetic,
};

[[nodiscard]] const char* to_string(CheckpointKind kind) noexcept;

// Lifecycle states are guarded: a checkpoint only becomes COMMITTED through the
// transactional publication path after every required condition is satisfied.
enum class CheckpointLifecycle : std::uint8_t {
    kCreating = 0,
    kWriting,
    kVerifying,
    kCommitted,
    kDegraded,
    kStale,
    kInvalidated,
    kRestoring,
    kRetired,
    kGcEligible,
    kDeleted,
    kFailed,
};

[[nodiscard]] const char* to_string(CheckpointLifecycle state) noexcept;

enum class IntegrityState : std::uint8_t {
    kUnknown = 0,
    kUnverified,
    kVerified,
    kCorrupt,
    kMissing,
};

[[nodiscard]] const char* to_string(IntegrityState state) noexcept;

enum class Provenance : std::uint8_t {
    kMeasured = 0,
    kReported,
    kDerived,
    kSynthetic,
    kUnknown,
};

[[nodiscard]] const char* to_string(Provenance p) noexcept;

enum class StorageTierClass : std::uint8_t {
    kLocalFilesystem = 0,
    kLocalNvmeClass,
    kSharedFilesystemClass,
    kObjectStorageClass,
    kRemoteBlockClass,
    kArchivalClass,
    kSyntheticRemote,
    kUnknown,
};

[[nodiscard]] const char* to_string(StorageTierClass tier) noexcept;

enum class ReplicaDurabilityState : std::uint8_t {
    kUnderReplicated = 0,
    kHealthy,
    kDegraded,
    kRebuilding,
    kFailed,
};

[[nodiscard]] const char* to_string(ReplicaDurabilityState state) noexcept;

enum class RestorePriority : std::uint8_t {
    kCritical = 0,
    kHigh,
    kNormal,
    kLow,
    kBackground,
};

[[nodiscard]] const char* to_string(RestorePriority priority) noexcept;

enum class RetentionClass : std::uint8_t {
    kPinned = 0,
    kKeepLatestN,
    kTtl,
    kAncestryRequired,
    kRestorePoint,
    kRecomputable,
    kGcEligible,
};

[[nodiscard]] const char* to_string(RetentionClass c) noexcept;

enum class Freshness : std::uint8_t {
    kCurrent = 0,
    kStale,
    kRevalidationRequired,
    kUnknown,
};

[[nodiscard]] const char* to_string(Freshness f) noexcept;

enum class DurabilityClass : std::uint8_t {
    kUnknown = 0,
    kEphemeral,
    kLocal,
    kReplicated,
    kArchival,
};

[[nodiscard]] const char* to_string(DurabilityClass d) noexcept;

enum class ReplicaSourceKind : std::uint8_t {
    kLocal = 0,
    kSyntheticRemote,
    kSyntheticObject,
    kSyntheticArchive,
};

[[nodiscard]] const char* to_string(ReplicaSourceKind k) noexcept;

// ==========================================================================
// Identity-derived address helpers
// ==========================================================================

// Content address of a chunk/blob: the SHA-256 of the physical bytes.
[[nodiscard]] crypto::Sha256Digest content_address(ByteView data);

// Derives a stable BlobId from a verified digest. Identical content maps to an
// identical BlobId so deduplication is deterministic and collision-free unless
// SHA-256 is broken.
[[nodiscard]] BlobId blob_id_from_digest(const crypto::Sha256Digest& digest) noexcept;

// Derives a stable ChunkId from a verified digest.
[[nodiscard]] ChunkId chunk_id_from_digest(const crypto::Sha256Digest& digest) noexcept;

// ==========================================================================
// Checkpoint descriptor
// ==========================================================================

struct CheckpointDescriptor {
    CheckpointId id;
    CheckpointFamilyId family_id;
    CheckpointGeneration generation;
    CheckpointKind kind = CheckpointKind::kFull;

    std::uint64_t logical_size = 0;
    std::chrono::system_clock::time_point created_at{};
    OwnerId owner_id;
    WorkerBootId producer_boot;
    CheckpointGeneration producer_generation;

    // Incremental / differential linkage. When only manifest-level parent/base
    // relationships are recorded (no application-level delta encoding), parent
    // and base refer to the logical ancestry the checkpoint extends.
    std::optional<CheckpointId> parent_checkpoint;
    std::optional<CheckpointId> base_checkpoint;

    std::vector<CheckpointId> lineage;      // ancestry references, ordered base->derived
    std::vector<CheckpointId> references;   // explicit dependency references

    std::string compatibility;              // opaque compatibility metadata
    RetentionClass retention = RetentionClass::kKeepLatestN;
    DurabilityClass durability = DurabilityClass::kLocal;
    RestorePriority restore_priority = RestorePriority::kNormal;
    Provenance provenance = Provenance::kUnknown;
    PolicyGeneration policy_generation;
    std::uint64_t required_replica_count = 1;
};

// ==========================================================================
// Chunk / blob model
// ==========================================================================

struct ChunkDescriptor {
    ChunkId id;
    ChunkGeneration generation;
    std::uint64_t logical_offset = 0;
    std::uint64_t logical_size = 0;
    std::uint64_t physical_size = 0;
    crypto::Sha256Digest digest{};
    BlobId blob_id;
    std::vector<PlacementId> placements;
    std::uint64_t refcount = 0;
    Provenance provenance = Provenance::kUnknown;
    IntegrityState integrity = IntegrityState::kUnknown;
    bool compressed = false;         // only meaningful if compression is shipped
    std::string payload_encoding;    // e.g. "identity" when not compressed
};

struct BlobDescriptor {
    BlobId id;
    BlobGeneration generation;
    std::uint64_t physical_size = 0;
    crypto::Sha256Digest digest{};
    std::uint64_t refcount = 0;                     // logical references
    std::uint64_t byte_references = 0;              // cumulative retained bytes
    StorageTierClass tier = StorageTierClass::kLocalFilesystem;
    Provenance provenance = Provenance::kUnknown;
    IntegrityState integrity = IntegrityState::kUnknown;
    Freshness freshness = Freshness::kUnknown;
    std::vector<ReplicaId> replicas;
};

// ==========================================================================
// Manifest
// ==========================================================================

struct ManifestEntry {
    ChunkId chunk_id;
    std::uint64_t logical_offset = 0;
};

struct CheckpointManifest {
    ManifestId id;
    ManifestGeneration generation;
    CheckpointId checkpoint_id;
    CheckpointGeneration checkpoint_generation;
    std::vector<ManifestEntry> chunks;    // ordered by logical_offset (deterministic)
    std::uint64_t logical_size = 0;
    crypto::Sha256Digest checkpoint_digest{};
    std::vector<CheckpointId> lineage;
    std::optional<CheckpointId> parent;
    std::optional<CheckpointId> base;
    Provenance provenance = Provenance::kUnknown;
    DurabilityClass durability = DurabilityClass::kLocal;
    std::uint64_t required_replica_count = 1;
    crypto::Sha256Digest semantic_digest{};
};

// ==========================================================================
// Replication
// ==========================================================================

enum class ReplicaRole : std::uint8_t {
    kAuthoritative = 0,
    kSecondary,
    kCandidate,
    kQuarantined,
};

[[nodiscard]] const char* to_string(ReplicaRole role) noexcept;

struct PlacementDescriptor {
    PlacementId id;
    PlacementGeneration generation;
    ReplicaId replica_id;
    StorageTierId tier_id;
    StorageBackendId backend_id;
    StorageNodeId node_id;
    VolumeId volume_id;
    std::uint64_t bytes = 0;
    Provenance provenance = Provenance::kUnknown;
    Freshness freshness = Freshness::kUnknown;
};

struct ReplicaDescriptor {
    ReplicaId id;
    ReplicaGeneration generation;
    StorageBackendId backend_id;
    ReplicaSourceKind source_kind = ReplicaSourceKind::kLocal;
    StorageTierClass tier = StorageTierClass::kLocalFilesystem;
    IntegrityState integrity = IntegrityState::kUnknown;
    ReplicaRole role = ReplicaRole::kAuthoritative;
    std::uint64_t physical_size = 0;
    Provenance provenance = Provenance::kUnknown;
};

struct ReplicaSet {
    CheckpointId checkpoint_id;
    CheckpointGeneration generation;
    std::uint64_t required_replica_count = 1;
    std::vector<ReplicaId> replicas;
    ReplicaDurabilityState durability_state = ReplicaDurabilityState::kUnderReplicated;
    PlacementGeneration placement_generation;
    Provenance provenance = Provenance::kUnknown;
};

// ==========================================================================
// Tiers
// ==========================================================================

struct TierMetadata {
    StorageTierId id;
    StorageTierClass tier_class = StorageTierClass::kUnknown;
    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::uint64_t latency_us = 0;      // observed or reported, see provenance
    std::uint64_t throughput_bytes_per_s = 0;
    DurabilityClass durability = DurabilityClass::kUnknown;
    Provenance provenance = Provenance::kUnknown;
    Freshness freshness = Freshness::kUnknown;
    std::string locality;
    std::string failure_domain;
    std::string cost_class;
    bool health = false;
};

// ==========================================================================
// Retention
// ==========================================================================

struct RetentionPolicy {
    RetentionPolicyId id;
    CheckpointFamilyId family_id;
    RetentionClass retention_class = RetentionClass::kKeepLatestN;
    std::uint64_t latest_n = 0;
    std::chrono::seconds ttl{0};
    bool protect_ancestry = true;
    bool recomputable = false;
    bool policy_protected = false;
    PolicyGeneration generation;
    Provenance provenance = Provenance::kUnknown;
};

// ==========================================================================
// Restore
// ==========================================================================

enum class RestoreSourceResult : std::uint8_t {
    kSuccess = 0,
    kCorrupt,
    kMissing,
    kUnavailable,
    kFallback,
};

[[nodiscard]] const char* to_string(RestoreSourceResult r) noexcept;

struct RestoreStep {
    CheckpointId checkpoint_id;
    ChunkId chunk_id;
    ReplicaId source_replica;
    RestoreSourceResult result = RestoreSourceResult::kSuccess;
    IntegrityState integrity = IntegrityState::kUnknown;
};

struct RestorePlan {
    RestorePlanId id;
    RestoreId restore_id;
    CheckpointId target;
    RestorePriority priority = RestorePriority::kNormal;
    std::vector<CheckpointId> ordered_ancestry;   // base -> target
    std::vector<RestoreStep> steps;
    std::uint64_t expected_bytes = 0;
    std::uint64_t parallelism = 1;
    RestoreGeneration generation;
    Provenance provenance = Provenance::kUnknown;
};

struct RestoreEvidence {
    RestoreId restore_id;
    CheckpointId source_checkpoint;
    ManifestId source_manifest;
    ReplicaId source_replica;
    std::uint64_t bytes_restored = 0;
    std::uint64_t chunks_restored = 0;
    std::uint64_t dedup_hits = 0;
    std::vector<ReplicaId> failed_sources;
    std::optional<ReplicaId> fallback_source;
    IntegrityState integrity = IntegrityState::kUnknown;
    std::chrono::milliseconds duration{0};
    Provenance provenance = Provenance::kUnknown;
    AttemptGeneration attempt_generation;
};

// ==========================================================================
// GC
// ==========================================================================

enum class GcPhase : std::uint8_t {
    kIdle = 0,
    kSnapshotting,
    kMarking,
    kSweeping,
    kComplete,
    kFailed,
};

[[nodiscard]] const char* to_string(GcPhase p) noexcept;

struct GcState {
    GcEpochId epoch;
    GcGeneration generation;
    GcPhase phase = GcPhase::kIdle;
    std::vector<BlobId> reachable_blobs;
    std::vector<BlobId> reclaimable_blobs;
    std::uint64_t marked_bytes = 0;
    std::uint64_t reclaimed_bytes = 0;
    Freshness freshness = Freshness::kUnknown;
};

// ==========================================================================
// Capacity / reservations
// ==========================================================================

struct CapacityReport {
    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::uint64_t reserved_bytes = 0;
    std::uint64_t committed_bytes = 0;
    std::uint64_t reclaimable_bytes = 0;
    std::uint64_t logical_bytes = 0;
    std::uint64_t deduplicated_bytes = 0;
    Freshness freshness = Freshness::kUnknown;
    Provenance provenance = Provenance::kUnknown;
};

// ==========================================================================
// Accounting
// ==========================================================================

struct AccountingSnapshot {
    std::uint64_t family_count = 0;
    std::uint64_t checkpoint_count = 0;
    std::uint64_t committed_checkpoint_count = 0;
    std::uint64_t logical_bytes = 0;
    std::uint64_t unique_physical_bytes = 0;
    std::uint64_t deduplicated_bytes = 0;
    std::uint64_t manifest_count = 0;
    std::uint64_t chunk_count = 0;
    std::uint64_t blob_count = 0;
    std::uint64_t blob_references = 0;
    std::uint64_t replica_count = 0;
    std::uint64_t placement_count = 0;
    std::uint64_t active_writes = 0;
    std::uint64_t active_restores = 0;
    std::uint64_t restored_bytes = 0;
    std::uint64_t gc_eligible_checkpoints = 0;
    std::uint64_t reclaimed_blobs = 0;
    std::uint64_t reclaimed_bytes = 0;
    std::uint64_t integrity_failures = 0;
    std::uint64_t stale_rejections = 0;
    std::uint64_t duplicate_rejections = 0;
    std::uint64_t worker_restarts = 0;
};

}  // namespace checkpointstore

#endif
