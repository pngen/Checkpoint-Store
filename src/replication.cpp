#include <checkpointstore/model.hpp>

#include <vector>

namespace checkpointstore {

// Evaluates the replication durability state of a replica set given the
// integrity of each member. A checkpoint that requires N replicas is only
// durability-satisfied when at least N replicas are verified and authoritative.
ReplicaDurabilityState evaluate_replica_durability(std::uint64_t required_count,
                                                   const std::vector<ReplicaDescriptor>& replicas) {
    std::uint64_t verified = 0;
    bool any_corrupt = false;
    bool any_missing = false;
    for (const auto& r : replicas) {
        if (r.integrity == IntegrityState::kVerified) {
            ++verified;
        } else if (r.integrity == IntegrityState::kCorrupt) {
            any_corrupt = true;
        } else if (r.integrity == IntegrityState::kMissing) {
            any_missing = true;
        }
    }
    if (verified >= required_count) {
        return ReplicaDurabilityState::kHealthy;
    }
    if (any_corrupt) {
        return ReplicaDurabilityState::kDegraded;
    }
    if (any_missing) {
        return ReplicaDurabilityState::kRebuilding;
    }
    return ReplicaDurabilityState::kUnderReplicated;
}

}  // namespace checkpointstore
