#include "test_util.hpp"
#include <checkpointstore/identity/identities.hpp>
#include <checkpointstore/identity/generations.hpp>
#include <checkpointstore/base/strong.hpp>
#include <type_traits>

using namespace checkpointstore;

// Strong-type compile-time guarantees.
static_assert(!std::is_convertible_v<CheckpointId, ChunkId>, "checkpoint id != chunk id");
static_assert(!std::is_convertible_v<ManifestId, BlobId>, "manifest id != blob id");
static_assert(!std::is_convertible_v<CheckpointGeneration, ManifestGeneration>, "generations not interchangeable");
static_assert(std::is_same_v<BasicId<detail::CheckpointIdTag>, CheckpointId>, "alias matches");

int main() {
    CheckpointId a(5);
    CheckpointId b(5);
    CheckpointId c(9);
    CHECK_EQ(a.value(), 5u);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a < c);          // spaceship total order
    CHECK(c > a);
    CHECK(a.is_valid());
    CHECK(!CheckpointId().is_valid());
    CHECK_EQ(to_hex_string(a), std::string("0000000000000005"));

    CheckpointGeneration g1(10), g2(20);
    CHECK(g1.precedes(g2));
    CHECK(g2.follows(g1));
    CHECK(g1.equal_or_precedes(g2));
    CHECK(g2.next().value() == 21u);
    // Generation comparisons must be explicit; the spaceship operator is still
    // available for ordered containers.
    CHECK(g1 < g2);

    // Authority is incarnation-scoped: a stale generation alone cannot fence a
    // fresh boot.
    WorkerBootId boot_old(1), boot_new(2);
    ScopedAuthority<CheckpointGeneration> auth_old(boot_old, CheckpointGeneration(99));
    ScopedAuthority<CheckpointGeneration> auth_new(boot_new, CheckpointGeneration(1));
    CHECK(auth_old.boot() == boot_old);
    CHECK(auth_old.generation().value() == 99u);
    CHECK(auth_old != auth_new);   // different incarnations cannot be equal authority
    CHECK(auth_old.generation().follows(auth_new.generation()));
    // A numerically larger generation does not make an old-incarnation
    // authority current: the incursion check must compare boot identity first.
    CHECK(auth_old.boot() != auth_new.boot());

    return cpstest::finish("test_identity");
}
