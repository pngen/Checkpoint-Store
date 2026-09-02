#ifndef CHECKPOINTSTORE_STORE_HPP
#define CHECKPOINTSTORE_STORE_HPP

#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/identity/generations.hpp>
#include <checkpointstore/identity/identities.hpp>
#include <checkpointstore/model.hpp>
#include <checkpointstore/storage/backend.hpp>
#include <checkpointstore/storage/dedup.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace checkpointstore {

struct StoreOptions {
    std::filesystem::path state_path;                 // metadata state file
    WorkerBootId boot_id;                             // live authority incarnation
    StorageBackendId primary_backend_id{1};           // default blob backend
    std::uint64_t chunk_size = 64 * 1024;             // fixed chunker size
    std::uint64_t required_replica_count = 1;         // default checkpoint durability
    std::uint64_t default_retention_latest_n = 5;
};

// A logically committed checkpoint returned by publication.
struct PublishedCheckpoint {
    CheckpointDescriptor descriptor;
    CheckpointManifest manifest;
    std::vector<ChunkDescriptor> chunks;
    std::uint64_t unique_physical_bytes = 0;
    std::uint64_t deduplicated_bytes = 0;
    std::uint64_t dedup_hits = 0;
    std::uint64_t dedup_misses = 0;
};

// Result of a restore.
struct RestoredCheckpoint {
    Bytes bytes;
    std::vector<RestoreStep> steps;
    RestoreEvidence evidence;
    IntegrityState integrity = IntegrityState::kUnknown;
};

// Result of a GC run.
struct GcRunResult {
    GcEpochId epoch;
    GcGeneration generation;
    std::uint64_t marked_bytes = 0;
    std::vector<BlobId> reclaimed_blobs;
    std::uint64_t reclaimed_bytes = 0;
    std::uint64_t dereferenced_blobs = 0;
    std::vector<std::string> notes;
};

// The Checkpoint Store runtime. It owns authoritative checkpoint metadata,
// physical blob placement, deduplication, replication state, retention,
// garbage collection, restore planning, and recovery. It is guarded by an
// internal mutex; backend I/O and hashing are performed outside the lock
// wherever possible to avoid holding a global lock during slow operations.
class CheckpointStore {
public:
    explicit CheckpointStore(StoreOptions options);
    ~CheckpointStore();

    CheckpointStore(const CheckpointStore&) = delete;
    CheckpointStore& operator=(const CheckpointStore&) = delete;
    CheckpointStore(CheckpointStore&&) noexcept;
    CheckpointStore& operator=(CheckpointStore&&) noexcept;

    // ------------------------------------------------------------------
    // Backends
    // ------------------------------------------------------------------
    void register_backend(StorageBackendId id, BackendPtr backend);
    [[nodiscard]] BackendPtr backend(StorageBackendId id);
    std::vector<BackendDescriptor> backend_descriptors() const;

    // ------------------------------------------------------------------
    // Families
    // ------------------------------------------------------------------
    CheckpointFamilyId create_family(OwnerId owner, Provenance provenance = Provenance::kUnknown);
    [[nodiscard]] bool family_exists(CheckpointFamilyId id) const;

    // ------------------------------------------------------------------
    // Publication
    // ------------------------------------------------------------------
    // Publishes a checkpoint from an in-memory byte stream. The descriptor
    // carries producer authority (producer_boot and producer_generation).
    PublishedCheckpoint publish(const CheckpointDescriptor& descriptor, ByteView data);

    // Convenience: build a full checkpoint descriptor for a family at the next
    // generation with the given bytes.
    CheckpointDescriptor make_full_descriptor(CheckpointFamilyId family,
                                                            OwnerId owner,
                                                            std::uint64_t logical_size,
                                                            DurabilityClass durability = DurabilityClass::kLocal);

    // ------------------------------------------------------------------
    // Queries
    // ------------------------------------------------------------------
    [[nodiscard]] CheckpointDescriptor get_checkpoint(CheckpointId id) const;
    [[nodiscard]] CheckpointManifest get_manifest(CheckpointId id) const;
    [[nodiscard]] std::vector<ChunkDescriptor> get_chunks(CheckpointId id) const;
    std::vector<ReplicaDescriptor> get_replicas(CheckpointId id) const;
    [[nodiscard]] CheckpointLifecycle lifecycle(CheckpointId id) const;
    [[nodiscard]] bool exists(CheckpointId id) const;
    std::vector<CheckpointId> list_checkpoints() const;

    // ------------------------------------------------------------------
    // Verification
    // ------------------------------------------------------------------
    IntegrityState verify_checkpoint(CheckpointId id);
    IntegrityState verify_chunk(CheckpointId id, ChunkId chunk);

    // ------------------------------------------------------------------
    // Restore
    // ------------------------------------------------------------------
    RestorePlan plan_restore(CheckpointId id, RestorePriority priority = RestorePriority::kNormal);
    RestoredCheckpoint restore(RestoreId restore_id, CheckpointId id,
                                             RestorePriority priority = RestorePriority::kNormal);

    // ------------------------------------------------------------------
    // Retention / retirement
    // ------------------------------------------------------------------
    RetentionPolicy set_retention_policy(RetentionPolicy policy);
    std::vector<CheckpointId> gc_eligible_checkpoints() const;
    void retire(CheckpointId id);

    // ------------------------------------------------------------------
    // Garbage collection
    // ------------------------------------------------------------------
    GcRunResult gc_plan();
    GcRunResult gc_run();

    // ------------------------------------------------------------------
    // Accounting
    // ------------------------------------------------------------------
    [[nodiscard]] AccountingSnapshot accounting() const;
    [[nodiscard]] DedupAccounting dedup_accounting() const;
    [[nodiscard]] CapacityReport capacity_report() const;

    // ------------------------------------------------------------------
    // Persistence / recovery
    // ------------------------------------------------------------------
    void save_state();
    void load_state();
    void reset_authority();
    void set_authority_boot(WorkerBootId boot);

    // ------------------------------------------------------------------
    // Authority / explanation helpers
    // ------------------------------------------------------------------
    [[nodiscard]] WorkerBootId boot_id() const noexcept { return options_.boot_id; }
    [[nodiscard]] CoordinatorEpoch coordinator_epoch() const noexcept { return coordinator_epoch_; }
    void set_coordinator_epoch(CoordinatorEpoch epoch) noexcept;
    [[nodiscard]] const DedupTable& dedup_table() const noexcept { return dedup_; }
    [[nodiscard]] std::string explain_integrity(CheckpointId id) const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    std::vector<CheckpointId> gc_eligible_checkpoints_unlocked() const;
    std::uint64_t gc_eligible_count_unlocked() const;

    StoreOptions options_;
    CoordinatorEpoch coordinator_epoch_{1};
    DedupTable dedup_;
};

}  // namespace checkpointstore

#endif