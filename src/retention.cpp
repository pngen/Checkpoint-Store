#include <checkpointstore/store.hpp>

#include "impl.hpp"

#include <checkpointstore/base/error.hpp>

#include <algorithm>
#include <chrono>
#include <map>
#include <set>
#include <vector>

namespace checkpointstore {

namespace {

// Returns true if the checkpoint is protected from GC (i.e. must be retained).
bool is_protected(const CheckpointStore::Impl::CheckpointRecord& rec,
                  const RetentionPolicy* policy,
                  const std::map<std::uint64_t, std::vector<CheckpointId>>& family_by_gen,
                  bool has_active_restore) {
    // Hard protections apply regardless of lifecycle: these can never be
    // overridden by an explicit retire.
    if (rec.replica_set.durability_state == ReplicaDurabilityState::kUnderReplicated) {
        return true;
    }
    if (has_active_restore) {
        return true;
    }
    if (rec.descriptor.retention == RetentionClass::kPinned ||
        rec.descriptor.retention == RetentionClass::kRestorePoint) {
        return true;
    }
    if (policy != nullptr && policy->policy_protected) {
        return true;
    }

    // An explicit retire overrides automatic retention (latest-N, TTL) so the
    // checkpoint becomes a GC candidate. Hard protections above still apply.
    const bool retired = rec.lifecycle == CheckpointLifecycle::kRetired ||
                         rec.lifecycle == CheckpointLifecycle::kGcEligible ||
                         rec.lifecycle == CheckpointLifecycle::kDeleted;
    if (retired) {
        return false;
    }

    // KEEP_LATEST_N is per family, ordered by generation (latest = last).
    if (rec.descriptor.retention == RetentionClass::kKeepLatestN) {
        const auto fam = rec.descriptor.family_id.value();
        auto git = family_by_gen.find(fam);
        if (git == family_by_gen.end()) {
            return true;  // conservative: no family data -> protect
        }
        const auto& ids = git->second;
        const std::uint64_t latest_n = (policy != nullptr && policy->latest_n > 0)
                                           ? policy->latest_n
                                           : 1;
        if (ids.size() <= latest_n) {
            return true;
        }
        // ids is sorted by generation ascending. Protected if it is among the
        // last latest_n entries.
        const std::uint64_t slot = static_cast<std::uint64_t>(
            std::find(ids.begin(), ids.end(), rec.descriptor.id) - ids.begin());
        if (slot + latest_n >= ids.size()) {
            return true;  // within the retained latest-N
        }
    }

    // TTL protects an unexpired checkpoint.
    if (rec.descriptor.retention == RetentionClass::kTtl) {
        if (policy != nullptr && policy->ttl.count() > 0) {
            const auto now = std::chrono::system_clock::now();
            if (now < rec.descriptor.created_at + policy->ttl) {
                return true;
            }
        } else {
            return true;  // no TTL configured -> conservative protect
        }
    }
    return false;
}

// Builds per-family checkpoint id lists sorted by generation ascending so that
// latest-N can be evaluated in generation order, not id order.
std::map<std::uint64_t, std::vector<CheckpointId>> build_family_generations(
    const CheckpointStore::Impl& impl) {
    std::map<std::uint64_t, std::vector<CheckpointId>> out;
    for (const auto& [fid, fam] : impl.families) {
        (void)fid;
        auto& ids = out[fam.id.value()];
        ids = fam.checkpoint_ids;
        std::sort(ids.begin(), ids.end(), [&](CheckpointId a, CheckpointId b) {
            auto ga = impl.checkpoints.find(a);
            auto gb = impl.checkpoints.find(b);
            if (ga == impl.checkpoints.end() || gb == impl.checkpoints.end()) {
                return a.value() < b.value();
            }
            return ga->second.descriptor.generation.value() <
                   gb->second.descriptor.generation.value();
        });
    }
    return out;
}

RetentionPolicy* find_policy(const CheckpointStore::Impl& impl, CheckpointFamilyId family) {
    for (const auto& [pid, p] : impl.retention) {
        (void)pid;
        if (p.family_id == family) {
            // const_cast for convenience; policies are effectively immutable.
            return const_cast<RetentionPolicy*>(&p);
        }
    }
    return nullptr;
}

std::set<CheckpointId> compute_protected(const CheckpointStore::Impl& impl) {
    const auto family_by_gen = build_family_generations(impl);
    std::set<CheckpointId> protected_set;
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& [cid, rec] : impl.checkpoints) {
            if (protected_set.count(cid)) {
                continue;
            }
            const RetentionPolicy* policy = find_policy(impl, rec.descriptor.family_id);
            if (is_protected(rec, policy, family_by_gen, false)) {
                protected_set.insert(cid);
                changed = true;
            }
        }
        // Ancestry protection: any checkpoint that is an ancestor of a protected
        // checkpoint (in its manifest lineage) is itself protected.
        for (const auto& [cid, rec] : impl.checkpoints) {
            if (!protected_set.count(cid)) {
                continue;
            }
            for (const auto& anc : rec.manifest.lineage) {
                if (impl.checkpoints.count(anc) && protected_set.insert(anc).second) {
                    changed = true;
                }
            }
        }
    }
    return protected_set;
}

}  // namespace

namespace detail {
std::set<CheckpointId> compute_protected_set(const CheckpointStore::Impl& impl) {
    return compute_protected(impl);
}
}  // namespace detail

RetentionPolicy CheckpointStore::set_retention_policy(RetentionPolicy policy) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    policy.generation = PolicyGeneration(1);
    impl_->retention[policy.id] = policy;
    return policy;
}

std::vector<CheckpointId> CheckpointStore::gc_eligible_checkpoints() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return gc_eligible_checkpoints_unlocked();
}

std::vector<CheckpointId> CheckpointStore::gc_eligible_checkpoints_unlocked() const {
    const auto protected_set = detail::compute_protected_set(*impl_);
    std::vector<CheckpointId> eligible;
    for (const auto& [cid, rec] : impl_->checkpoints) {
        if (rec.lifecycle != CheckpointLifecycle::kCommitted &&
            rec.lifecycle != CheckpointLifecycle::kRetired) {
            continue;  // only committed or retired checkpoints are GC candidates
        }
        if (protected_set.count(cid)) {
            continue;
        }
        eligible.push_back(cid);
    }
    return eligible;
}

std::uint64_t CheckpointStore::gc_eligible_count_unlocked() const {
    return static_cast<std::uint64_t>(gc_eligible_checkpoints_unlocked().size());
}

void CheckpointStore::retire(CheckpointId id) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    if (it->second.lifecycle != CheckpointLifecycle::kCommitted) {
        throw_error(ErrorCode::kTransaction, "only a committed checkpoint can be retired");
    }
    it->second.lifecycle = CheckpointLifecycle::kRetired;
    impl_->counters.gc_eligible_checkpoints = gc_eligible_count_unlocked();
}

}  // namespace checkpointstore