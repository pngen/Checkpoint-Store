#ifndef CHECKPOINTSTORE_IMPL_HPP
#define CHECKPOINTSTORE_IMPL_HPP

#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/model.hpp>
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/backend.hpp>
#include <checkpointstore/storage/dedup.hpp>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace checkpointstore {

// Internal runtime state for the CheckpointStore. Kept in an internal header so
// the persistence layer can serialize it. All access is guarded by the store's
// mutex held by the public methods; these maps are never touched directly by
// callers.
struct CheckpointStore::Impl {
    struct FamilyRecord {
        CheckpointFamilyId id;
        CheckpointFamilyGeneration family_generation;
        std::vector<CheckpointId> checkpoint_ids;  // in creation order
    };

    struct CheckpointRecord {
        CheckpointDescriptor descriptor;
        CheckpointManifest manifest;
        std::vector<ChunkDescriptor> chunks;
        ReplicaSet replica_set;
        CheckpointLifecycle lifecycle = CheckpointLifecycle::kCreating;
        IntegrityState integrity = IntegrityState::kUnknown;
        Freshness freshness = Freshness::kUnknown;
    };

    // Backends by id.
    std::unordered_map<StorageBackendId, BackendPtr> backends;
    StorageBackendId primary_backend_id;

    // Authoritative logical state.
    std::unordered_map<CheckpointFamilyId, FamilyRecord> families;
    std::unordered_map<CheckpointId, CheckpointRecord> checkpoints;
    std::unordered_map<BlobId, BlobDescriptor> blobs;
    std::unordered_map<ReplicaId, ReplicaDescriptor> replicas;
    std::unordered_map<PlacementId, PlacementDescriptor> placements;
    std::unordered_map<RetentionPolicyId, RetentionPolicy> retention;
    std::unordered_map<WorkerBootId, std::uint64_t> workers;  // boot -> generation watermark

    // Generation counters per family.
    std::unordered_map<CheckpointFamilyId, CheckpointGeneration> next_checkpoint_generation;
    CheckpointFamilyId next_family_id{1};
    std::uint64_t next_id_counter = 1000;

    // GC / restore / reservation runtime state.
    GcState gc;
    std::unordered_map<RestorePlanId, RestorePlan> restore_plans;
    std::unordered_map<CheckpointId, RestoreEvidence> restore_evidence;

    // Runtime-only (not persisted) tracking.
    std::vector<RestoreId> active_restores;
    std::unordered_map<ReservationId, std::uint64_t> reservations;

    // Accounting counters (some derived on demand).
    AccountingSnapshot counters;

    // Guard for all store state. Kept inside the pimpl so the CheckpointStore is
    // movable; locks never cross a backend I/O or hashing boundary where possible.
    mutable std::mutex mutex_;
};

}  // namespace checkpointstore

namespace checkpointstore::detail {

// Computes the set of checkpoint ids protected from GC under the active
// retention policies. Deleted/retired-and-swept checkpoints are never treated
// as still retaining blobs. Shared by GC and retention logic.
[[nodiscard]] std::set<CheckpointId> compute_protected_set(const CheckpointStore::Impl& impl);

}  // namespace checkpointstore::detail

#endif
