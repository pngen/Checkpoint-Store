#include <checkpointstore/store.hpp>

#include "impl.hpp"

#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>

#include <algorithm>
#include <set>
#include <string>

namespace checkpointstore {

namespace {

// Computes the set of blobs reachable from the set of retained (non-GC-eligible)
// checkpoints. A blob is reachable if any retained checkpoint references it.
std::set<BlobId> compute_reachable(const CheckpointStore::Impl::CheckpointRecord* recs,
                                   std::size_t count,
                                   const std::set<CheckpointId>& retained) {
    std::set<BlobId> reachable;
    for (std::size_t i = 0; i < count; ++i) {
        if (!retained.count(recs[i].descriptor.id)) {
            continue;
        }
        for (const auto& cd : recs[i].chunks) {
            reachable.insert(cd.blob_id);
        }
    }
    return reachable;
}

}  // namespace

GcRunResult CheckpointStore::gc_plan() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    const auto eligible = gc_eligible_checkpoints_unlocked();

    // Retained = checkpoints protected from GC. Deleted/swept checkpoints are
    // never treated as retaining blobs, so a blob re-referenced only by a
    // deleted checkpoint is still reclaimable.
    const auto retained = detail::compute_protected_set(*impl_);
    std::vector<Impl::CheckpointRecord> recs;
    for (const auto& [cid, rec] : impl_->checkpoints) {
        (void)cid;
        recs.push_back(rec);
    }
    const auto reachable = compute_reachable(recs.data(), recs.size(), retained);

    impl_->gc.epoch = GcEpochId(impl_->next_id_counter++);
    impl_->gc.generation = impl_->gc.generation.next();
    impl_->gc.phase = GcPhase::kMarking;
    impl_->gc.reachable_blobs.assign(reachable.begin(), reachable.end());
    impl_->gc.marked_bytes = 0;
    for (const auto b : reachable) {
        auto bit = impl_->blobs.find(b);
        if (bit != impl_->blobs.end()) {
            impl_->gc.marked_bytes += bit->second.physical_size;
        }
    }
    impl_->gc.reclaimable_blobs.clear();
    for (const auto& [bid, bd] : impl_->blobs) {
        if (!reachable.count(bid)) {
            impl_->gc.reclaimable_blobs.push_back(bid);
        }
    }
    impl_->gc.freshness = Freshness::kCurrent;

    GcRunResult plan;
    plan.epoch = impl_->gc.epoch;
    plan.generation = impl_->gc.generation;
    plan.marked_bytes = impl_->gc.marked_bytes;
    return plan;
}

GcRunResult CheckpointStore::gc_run() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    const auto eligible = gc_eligible_checkpoints_unlocked();
    GcRunResult result;
    result.epoch = impl_->gc.epoch;
    result.generation = impl_->gc.generation;

    // Recompute reachable at delete time; retained = protected checkpoints
    // (deleted/swept checkpoints never retain blobs), so a stale snapshot can
    // never delete a blob that was re-referenced after the plan.
    const auto retained = detail::compute_protected_set(*impl_);
    std::vector<Impl::CheckpointRecord> recs;
    for (const auto& [cid, rec] : impl_->checkpoints) {
        (void)cid;
        recs.push_back(rec);
    }
    const auto reachable = compute_reachable(recs.data(), recs.size(), retained);

    // Sweep eligible checkpoints: release their chunk references.
    for (const auto& cid : eligible) {
        auto it = impl_->checkpoints.find(cid);
        if (it == impl_->checkpoints.end()) {
            continue;
        }
        if (it->second.lifecycle == CheckpointLifecycle::kGcEligible ||
            it->second.lifecycle == CheckpointLifecycle::kDeleted) {
            continue;
        }
        for (const auto& cd : it->second.chunks) {
            // Release one reference for this swept checkpoint.
            try {
                (void)dedup_.release_reference(cd.digest);
            } catch (const CheckpointStoreError&) {
                // Refcount disagreement: defer (conservative).
                result.notes.push_back("refcount disagreement deferred for " +
                                       crypto::hex(cd.digest));
            }
        }
        it->second.lifecycle = CheckpointLifecycle::kDeleted;
        if (impl_->counters.gc_eligible_checkpoints > 0) {
            --impl_->counters.gc_eligible_checkpoints;
        }
        ++result.dereferenced_blobs;
    }

    // Now reclaim orphan blobs that are no longer referenced by anything.
    for (const auto& bid : impl_->gc.reclaimable_blobs) {
        if (reachable.count(bid)) {
            result.notes.push_back("deferred: blob reachable in current snapshot");
            continue;
        }
        auto bit = impl_->blobs.find(bid);
        if (bit == impl_->blobs.end()) {
            continue;
        }
        const auto& bd = bit->second;
        if (dedup_.refcount(bd.digest) != 0) {
            result.notes.push_back("deferred: refcount not zero");
            continue;
        }
        // Physically remove from each replica backend.
        bool removed_any = false;
        bool all_removed = true;
        for (const auto rid : bd.replicas) {
            auto rit = impl_->replicas.find(rid);
            if (rit == impl_->replicas.end()) {
                continue;
            }
            auto bbit = impl_->backends.find(rit->second.backend_id);
            if (bbit == impl_->backends.end()) {
                all_removed = false;
                continue;
            }
            const BackendKey key{"blobs/" + crypto::hex(bd.digest).substr(0, 2) + "/" +
                                 crypto::hex(bd.digest)};
            if (bbit->second->remove(key)) {
                removed_any = true;
            } else {
                all_removed = false;
            }
        }
        if (removed_any) {
            result.reclaimed_blobs.push_back(bid);
            result.reclaimed_bytes += bd.physical_size;
            ++impl_->counters.reclaimed_blobs;
            impl_->counters.reclaimed_bytes += bd.physical_size;
            (void)dedup_.release_until_orphan(bd.digest);
        }
        if (!all_removed) {
            result.notes.push_back("partial removal; retry in next pass");
        }
        impl_->blobs.erase(bid);
    }

    impl_->gc.phase = GcPhase::kComplete;
    impl_->gc.reclaimable_blobs.clear();
    impl_->gc.reachable_blobs.clear();
    impl_->counters.blob_count = static_cast<std::uint64_t>(impl_->blobs.size());
    impl_->counters.unique_physical_bytes = dedup_.unique_physical_bytes();
    return result;
}

}  // namespace checkpointstore