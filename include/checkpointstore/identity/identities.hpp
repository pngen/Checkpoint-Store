#ifndef CHECKPOINTSTORE_IDENTITY_IDENTITIES_HPP
#define CHECKPOINTSTORE_IDENTITY_IDENTITIES_HPP

#include <checkpointstore/base/strong.hpp>

#include <cstdint>
#include <string>

namespace checkpointstore {
namespace detail {
struct CheckpointIdTag {};
struct CheckpointVersionIdTag {};
struct CheckpointFamilyIdTag {};
struct ManifestIdTag {};
struct ChunkIdTag {};
struct BlobIdTag {};
struct ReplicaIdTag {};
struct PlacementIdTag {};
struct StorageTierIdTag {};
struct StorageBackendIdTag {};
struct StorageNodeIdTag {};
struct VolumeIdTag {};
struct RestoreIdTag {};
struct RestorePlanIdTag {};
struct GcEpochIdTag {};
struct RetentionPolicyIdTag {};
struct OwnerIdTag {};
struct WorkerIdTag {};
struct WorkerBootIdTag {};
struct AttemptIdTag {};
struct DispatchIdTag {};
struct ReservationIdTag {};
struct ObservationIdTag {};
}  // namespace detail

// Identities are strong, non-interchangeable value types. Two identities of
// different kinds cannot be compared, mixed, or implicitly converted.
using CheckpointId = BasicId<detail::CheckpointIdTag>;
using CheckpointVersionId = BasicId<detail::CheckpointVersionIdTag>;
using CheckpointFamilyId = BasicId<detail::CheckpointFamilyIdTag>;
using ManifestId = BasicId<detail::ManifestIdTag>;
using ChunkId = BasicId<detail::ChunkIdTag>;
using BlobId = BasicId<detail::BlobIdTag>;
using ReplicaId = BasicId<detail::ReplicaIdTag>;
using PlacementId = BasicId<detail::PlacementIdTag>;
using StorageTierId = BasicId<detail::StorageTierIdTag>;
using StorageBackendId = BasicId<detail::StorageBackendIdTag>;
using StorageNodeId = BasicId<detail::StorageNodeIdTag>;
using VolumeId = BasicId<detail::VolumeIdTag>;
using RestoreId = BasicId<detail::RestoreIdTag>;
using RestorePlanId = BasicId<detail::RestorePlanIdTag>;
using GcEpochId = BasicId<detail::GcEpochIdTag>;
using RetentionPolicyId = BasicId<detail::RetentionPolicyIdTag>;
using OwnerId = BasicId<detail::OwnerIdTag>;
using WorkerId = BasicId<detail::WorkerIdTag>;
using WorkerBootId = BasicId<detail::WorkerBootIdTag>;
using AttemptId = BasicId<detail::AttemptIdTag>;
using DispatchId = BasicId<detail::DispatchIdTag>;
using ReservationId = BasicId<detail::ReservationIdTag>;
using ObservationId = BasicId<detail::ObservationIdTag>;

// Renders an identity as a zero-padded, lower-case hexadecimal string. The
// hexadecimal form is stable and used for persistence-friendly key derivation.
[[nodiscard]] std::string to_hex_string(std::uint64_t value);

template <typename Tag>
[[nodiscard]] std::string to_hex_string(const BasicId<Tag>& id) {
    return to_hex_string(id.value());
}

}  // namespace checkpointstore

#endif
