#ifndef CHECKPOINTSTORE_IDENTITY_GENERATIONS_HPP
#define CHECKPOINTSTORE_IDENTITY_GENERATIONS_HPP

#include <checkpointstore/base/strong.hpp>
#include <checkpointstore/identity/identities.hpp>

#include <cstdint>

namespace checkpointstore {
namespace detail {
struct CoordinatorEpochTag {};
struct CheckpointGenerationTag {};
struct CheckpointFamilyGenerationTag {};
struct ManifestGenerationTag {};
struct ChunkGenerationTag {};
struct BlobGenerationTag {};
struct ReplicaGenerationTag {};
struct PlacementGenerationTag {};
struct BackendGenerationTag {};
struct VolumeGenerationTag {};
struct RestoreGenerationTag {};
struct GcGenerationTag {};
struct RetentionGenerationTag {};
struct AttemptGenerationTag {};
struct DispatchGenerationTag {};
struct ObservationGenerationTag {};
struct PolicyGenerationTag {};
}  // namespace detail

// Generations are strong and explicitly ordered. Comparisons are explicit
// (precedes/follows/equal_to) so callers state intent. A generation number is
// not authority by itself: see AuthorityTag in base/strong.hpp.
using CoordinatorEpoch = BasicGeneration<detail::CoordinatorEpochTag>;
using CheckpointGeneration = BasicGeneration<detail::CheckpointGenerationTag>;
using CheckpointFamilyGeneration = BasicGeneration<detail::CheckpointFamilyGenerationTag>;
using ManifestGeneration = BasicGeneration<detail::ManifestGenerationTag>;
using ChunkGeneration = BasicGeneration<detail::ChunkGenerationTag>;
using BlobGeneration = BasicGeneration<detail::BlobGenerationTag>;
using ReplicaGeneration = BasicGeneration<detail::ReplicaGenerationTag>;
using PlacementGeneration = BasicGeneration<detail::PlacementGenerationTag>;
using BackendGeneration = BasicGeneration<detail::BackendGenerationTag>;
using VolumeGeneration = BasicGeneration<detail::VolumeGenerationTag>;
using RestoreGeneration = BasicGeneration<detail::RestoreGenerationTag>;
using GcGeneration = BasicGeneration<detail::GcGenerationTag>;
using RetentionGeneration = BasicGeneration<detail::RetentionGenerationTag>;
using AttemptGeneration = BasicGeneration<detail::AttemptGenerationTag>;
using DispatchGeneration = BasicGeneration<detail::DispatchGenerationTag>;
using ObservationGeneration = BasicGeneration<detail::ObservationGenerationTag>;
using PolicyGeneration = BasicGeneration<detail::PolicyGenerationTag>;

// Authority combines an incarnation (WorkerBootId) with any generation kind so
// that a fresh process incarnation is never fenced by a stale numeric
// generation. A numeric generation alone does not carry authority.
template <typename Generation>
using ScopedAuthority = AuthorityTag<Generation, WorkerBootId>;

}  // namespace checkpointstore

#endif
