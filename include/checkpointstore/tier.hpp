#ifndef CHECKPOINTSTORE_TIER_HPP
#define CHECKPOINTSTORE_TIER_HPP

#include <checkpointstore/model.hpp>
#include <checkpointstore/storage/backend.hpp>

namespace checkpointstore {

// Truthfully classifies the storage tier of a backend descriptor.
[[nodiscard]] StorageTierClass classify_backend_tier(const BackendDescriptor& b);

// Builds deterministic synthetic tier metadata for an unavailable remote tier.
[[nodiscard]] TierMetadata synthetic_tier_metadata(StorageTierId id, StorageTierClass cls,
                                                   std::uint64_t total_bytes,
                                                   std::uint64_t latency_us);

}  // namespace checkpointstore
#endif
