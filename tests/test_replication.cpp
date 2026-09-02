#include "test_util.hpp"
#include <checkpointstore/replication.hpp>
#include <checkpointstore/model.hpp>

using namespace checkpointstore;

int main() {
    // Multi-replica durability.
    ReplicaDescriptor r1, r2, r3, r4;
    r1.id = ReplicaId(1); r1.integrity = IntegrityState::kVerified; r1.role = ReplicaRole::kAuthoritative;
    r2.id = ReplicaId(2); r2.integrity = IntegrityState::kVerified; r2.role = ReplicaRole::kSecondary;
    r3.id = ReplicaId(3); r3.integrity = IntegrityState::kCorrupt; r3.role = ReplicaRole::kAuthoritative;
    r4.id = ReplicaId(4); r4.integrity = IntegrityState::kMissing;

    // Requirement N=2 with 2 verified -> healthy.
    CHECK(evaluate_replica_durability(2, {r1, r2}) == ReplicaDurabilityState::kHealthy);
    // Requirement N=3 with 2 verified -> under-replicated.
    CHECK(evaluate_replica_durability(3, {r1, r2}) == ReplicaDurabilityState::kUnderReplicated);
    // A corrupt replica degrades.
    CHECK(evaluate_replica_durability(2, {r1, r3}) == ReplicaDurabilityState::kDegraded);
    // A missing replica -> rebuilding.
    CHECK(evaluate_replica_durability(2, {r1, r4}) == ReplicaDurabilityState::kRebuilding);
    // Zero replicas cannot satisfy any requirement above zero.
    CHECK(evaluate_replica_durability(1, {}) == ReplicaDurabilityState::kUnderReplicated);

    // Restore source result strings.
    CHECK(std::string(to_string(RestoreSourceResult::kCorrupt)) == "CORRUPT");
    CHECK(std::string(to_string(RestorePriority::kCritical)) == "CRITICAL");

    return cpstest::finish("test_replication");
}
