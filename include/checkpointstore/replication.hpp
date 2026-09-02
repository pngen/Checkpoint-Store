#ifndef CHECKPOINTSTORE_REPLICATION_HPP
#define CHECKPOINTSTORE_REPLICATION_HPP

#include <checkpointstore/model.hpp>

#include <vector>

namespace checkpointstore {

// Evaluates replication durability given a required replica count and the
// integrity of each member replica.
[[nodiscard]] ReplicaDurabilityState evaluate_replica_durability(
    std::uint64_t required_count, const std::vector<ReplicaDescriptor>& replicas);

}  // namespace checkpointstore
#endif
