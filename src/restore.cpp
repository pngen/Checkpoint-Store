#include <checkpointstore/store.hpp>

#include "impl.hpp"

#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/storage/backend.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>

namespace checkpointstore {

namespace {
BackendKey blob_key_for(const crypto::Sha256Digest& d) {
    return BackendKey{"blobs/" + crypto::hex(d).substr(0, 2) + "/" + crypto::hex(d)};
}
}  // namespace

IntegrityState CheckpointStore::verify_checkpoint(CheckpointId id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    auto& rec = it->second;
    IBackend* underlay = nullptr;
    for (const auto rid : rec.replica_set.replicas) {
        auto rit = impl_->replicas.find(rid);
        if (rit == impl_->replicas.end()) {
            continue;
        }
        auto bit = impl_->backends.find(rit->second.backend_id);
        if (bit != impl_->backends.end()) {
            underlay = bit->second.get();
            if (rit->second.integrity == IntegrityState::kVerified) {
                break;
            }
        }
    }
    if (underlay == nullptr) {
        rec.integrity = IntegrityState::kMissing;
        ++impl_->counters.integrity_failures;
        return rec.integrity;
    }
    crypto::Sha256Hasher combined;
    for (const auto& cd : rec.chunks) {
        const BackendKey key = blob_key_for(cd.digest);
        if (!underlay->exists(key)) {
            rec.integrity = IntegrityState::kMissing;
            ++impl_->counters.integrity_failures;
            return rec.integrity;
        }
        const IntegrityState v = underlay->verify(key, cd.digest);
        if (v == IntegrityState::kCorrupt || v == IntegrityState::kMissing) {
            rec.integrity = IntegrityState::kCorrupt;
            ++impl_->counters.integrity_failures;
            return rec.integrity;
        }
        Bytes rb = underlay->read(key);
        combined.update(ByteView(rb.data(), rb.size()));
    }
    const auto fd = combined.final();
    if (!crypto::equal(fd, rec.manifest.checkpoint_digest)) {
        rec.integrity = IntegrityState::kCorrupt;
        ++impl_->counters.integrity_failures;
        return rec.integrity;
    }
    rec.integrity = IntegrityState::kVerified;
    return rec.integrity;
}

IntegrityState CheckpointStore::verify_chunk(CheckpointId id, ChunkId chunk) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    auto& rec = it->second;
    for (auto& cd : rec.chunks) {
        if (cd.id != chunk) {
            continue;
        }
        auto bit = impl_->blobs.find(cd.blob_id);
        if (bit == impl_->blobs.end()) {
            return IntegrityState::kMissing;
        }
        IBackend* underlay = nullptr;
        for (const auto rid : bit->second.replicas) {
            auto rit = impl_->replicas.find(rid);
            if (rit == impl_->replicas.end()) {
                continue;
            }
            auto bbit = impl_->backends.find(rit->second.backend_id);
            if (bbit != impl_->backends.end()) {
                underlay = bbit->second.get();
                break;
            }
        }
        if (underlay == nullptr) {
            return IntegrityState::kMissing;
        }
        const BackendKey key = blob_key_for(cd.digest);
        const auto v = underlay->verify(key, cd.digest);
        cd.integrity = v;
        return v;
    }
    throw_error(ErrorCode::kNotFound, "chunk not found in checkpoint");
}

RestorePlan CheckpointStore::plan_restore(CheckpointId id, RestorePriority priority) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    auto& rec = it->second;
    RestorePlan plan;
    plan.id = RestorePlanId(impl_->next_id_counter++);
    plan.restore_id = RestoreId(impl_->next_id_counter++);
    plan.target = id;
    plan.priority = priority;
    plan.generation = RestoreGeneration(1);
    plan.provenance = Provenance::kMeasured;
    plan.ordered_ancestry = rec.manifest.lineage;
    for (const auto& entry : rec.manifest.chunks) {
        RestoreStep step;
        step.checkpoint_id = id;
        step.chunk_id = entry.chunk_id;
        for (const auto& cd : rec.chunks) {
            if (cd.id == entry.chunk_id && !cd.placements.empty()) {
                auto pit = impl_->placements.find(cd.placements.front());
                if (pit != impl_->placements.end()) {
                    step.source_replica = pit->second.replica_id;
                }
                break;
            }
        }
        step.result = RestoreSourceResult::kSuccess;
        plan.steps.push_back(step);
    }
    plan.expected_bytes = rec.descriptor.logical_size;
    plan.parallelism = 1;
    impl_->restore_plans[plan.id] = plan;
    return plan;
}

RestoredCheckpoint CheckpointStore::restore(RestoreId restore_id, CheckpointId id,
                                            RestorePriority priority) {
    const auto start = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    auto& rec = it->second;
    if (rec.lifecycle == CheckpointLifecycle::kGcEligible ||
        rec.lifecycle == CheckpointLifecycle::kDeleted) {
        throw_error(ErrorCode::kNotFound, "checkpoint no longer restorable");
    }
    (void)priority;

    Bytes out;
    out.reserve(static_cast<std::size_t>(rec.descriptor.logical_size));
    crypto::Sha256Hasher combined;
    RestoreEvidence evidence;
    evidence.restore_id = restore_id;
    evidence.source_checkpoint = id;
    evidence.source_manifest = rec.manifest.id;
    evidence.attempt_generation = AttemptGeneration(1);

    std::uint64_t bytes_restored = 0;
    std::uint64_t chunks_restored = 0;
    for (const auto& entry : rec.manifest.chunks) {
        ChunkDescriptor* cd = nullptr;
        for (auto& c : rec.chunks) {
            if (c.id == entry.chunk_id) {
                cd = &c;
                break;
            }
        }
        if (cd == nullptr) {
            throw_error(ErrorCode::kMalformed, "restore: chunk missing from record");
        }
        const BackendKey key = blob_key_for(cd->digest);
        bool restored = false;
        std::vector<ReplicaId> cand;
        for (const auto pid : cd->placements) {
            auto pit = impl_->placements.find(pid);
            if (pit != impl_->placements.end()) {
                cand.push_back(pit->second.replica_id);
            }
        }
        if (cand.empty()) {
            cand = rec.replica_set.replicas;
        }
        for (const auto rid : cand) {
            auto rit = impl_->replicas.find(rid);
            if (rit == impl_->replicas.end()) {
                continue;
            }
            auto bbit = impl_->backends.find(rit->second.backend_id);
            if (bbit == impl_->backends.end() || !bbit->second->health()) {
                evidence.failed_sources.push_back(rid);
                continue;
            }
            try {
                Bytes rb = bbit->second->read(key);
                const auto got = crypto::sha256(ByteView(rb.data(), rb.size()));
                if (crypto::equal(got, cd->digest)) {
                    restored = true;
                    out.insert(out.end(), rb.begin(), rb.end());
                    combined.update(ByteView(rb.data(), rb.size()));
                    evidence.source_replica = rid;
                    ++chunks_restored;
                    bytes_restored += rb.size();
                    break;
                }
                // Corrupt bytes on this replica; refuse, quarantine, try next.
                ++impl_->counters.integrity_failures;
                evidence.failed_sources.push_back(rid);
                if (rit->second.integrity == IntegrityState::kVerified) {
                    rit->second.integrity = IntegrityState::kCorrupt;
                    rit->second.role = ReplicaRole::kQuarantined;
                    rec.replica_set.durability_state = ReplicaDurabilityState::kDegraded;
                }
            } catch (const CheckpointStoreError& e) {
                evidence.failed_sources.push_back(rid);
                if (e.code() == ErrorCode::kNotFound) {
                    // missing source
                }
            }
        }
        if (!restored) {
            rec.integrity = IntegrityState::kCorrupt;
            throw_error(ErrorCode::kCorrupt,
                        "restore: no verified replica for a required chunk");
        }
    }

    const auto fd = combined.final();
    if (!crypto::equal(fd, rec.manifest.checkpoint_digest)) {
        rec.integrity = IntegrityState::kCorrupt;
        ++impl_->counters.integrity_failures;
        throw_error(ErrorCode::kDigestMismatch, "restored checkpoint digest mismatch");
    }
    rec.integrity = IntegrityState::kVerified;

    const auto end = std::chrono::steady_clock::now();
    evidence.bytes_restored = bytes_restored;
    evidence.chunks_restored = chunks_restored;
    evidence.integrity = IntegrityState::kVerified;
    evidence.duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    evidence.provenance = Provenance::kMeasured;
    impl_->restore_evidence[id] = evidence;
    impl_->counters.restored_bytes += bytes_restored;

    RestoredCheckpoint rc;
    rc.bytes = std::move(out);
    rc.evidence = evidence;
    rc.integrity = IntegrityState::kVerified;
    return rc;
}

}  // namespace checkpointstore