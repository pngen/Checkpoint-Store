#include <checkpointstore/store.hpp>

#include "impl.hpp"

#include <checkpointstore/base/error.hpp>

namespace checkpointstore {

AccountingSnapshot CheckpointStore::accounting() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->counters;
}

DedupAccounting CheckpointStore::dedup_accounting() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    DedupAccounting a;
    // Logical bytes are the sum of committed checkpoint logical sizes.
    std::uint64_t logical = 0;
    for (const auto& [id, rec] : impl_->checkpoints) {
        (void)id;
        if (rec.lifecycle == CheckpointLifecycle::kCommitted) {
            logical += rec.descriptor.logical_size;
        }
    }
    const std::uint64_t unique = dedup_.unique_physical_bytes();
    a.logical_bytes = logical;
    a.unique_physical_bytes = unique;
    a.deduplicated_bytes = logical >= unique ? (logical - unique) : 0;
    return a;
}

CapacityReport CheckpointStore::capacity_report() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    CapacityReport rep;
    // Aggregate primary backend capacity.
    auto it = impl_->backends.find(impl_->primary_backend_id);
    if (it != impl_->backends.end()) {
        TierMetadata m = it->second->query_capacity();
        rep.total_bytes = m.total_bytes;
        rep.free_bytes = m.free_bytes;
        rep.provenance = m.provenance;
        rep.freshness = m.freshness;
    }
    rep.committed_bytes = dedup_.unique_physical_bytes();
    rep.logical_bytes = 0;
    for (const auto& [id, rec] : impl_->checkpoints) {
        (void)id;
        if (rec.lifecycle == CheckpointLifecycle::kCommitted) {
            rep.logical_bytes += rec.descriptor.logical_size;
        }
    }
    rep.deduplicated_bytes = rep.logical_bytes >= rep.committed_bytes
                                ? (rep.logical_bytes - rep.committed_bytes)
                                : 0;
    rep.reserved_bytes = 0;
    for (const auto& [rid, bytes] : impl_->reservations) {
        (void)rid;
        rep.reserved_bytes += bytes;
    }
    // Reclaimable is derived from GC snapshot candidates when present.
    rep.reclaimable_bytes = 0;
    for (const auto& b : impl_->gc.reclaimable_blobs) {
        auto bit = impl_->blobs.find(b);
        if (bit != impl_->blobs.end()) {
            rep.reclaimable_bytes += bit->second.physical_size;
        }
    }
    return rep;
}

void CheckpointStore::reset_authority() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    // Live worker/process authority is cleared. Physical observation freshness
    // is forced to REVALIDATION_REQUIRED and active writes/restores become
    // recovery-required. Active GC snapshots cannot resume destructively.
    for (auto& [id, rec] : impl_->checkpoints) {
        (void)id;
        if (rec.lifecycle == CheckpointLifecycle::kWriting ||
            rec.lifecycle == CheckpointLifecycle::kCreating) {
            rec.lifecycle = CheckpointLifecycle::kFailed;
        }
    }
    for (auto& [id, b] : impl_->blobs) {
        (void)id;
        // Physical observation freshness is reset; the logical content digest is
        // unchanged but the stored bytes must be re-verified before use.
        b.freshness = Freshness::kRevalidationRequired;
        if (b.integrity != IntegrityState::kCorrupt) {
            b.integrity = IntegrityState::kUnverified;
        }
    }
    impl_->gc.phase = GcPhase::kIdle;
    impl_->active_restores.clear();
    ++impl_->counters.worker_restarts;
}

}  // namespace checkpointstore