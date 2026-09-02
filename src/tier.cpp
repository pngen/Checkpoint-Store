#include <checkpointstore/model.hpp>
#include <checkpointstore/storage/backend.hpp>

#include <string>

namespace checkpointstore {

// Classifies the physical storage tier of a backend descriptor truthfully. A
// tier is only classified as LOCAL_NVME_CLASS if the classification can be
// established; otherwise LOCAL_FILESYSTEM is used. Unavailable remote tiers are
// labeled SYNTHETIC_REMOTE with provenance SYNTHETIC.
StorageTierClass classify_backend_tier(const BackendDescriptor& b) {
    // We cannot establish a physical NVMe class without a reliable device
    // classification, so a local filesystem backend stays at LOCAL_FILESYSTEM.
    switch (b.tier_class) {
        case StorageTierClass::kLocalNvmeClass:
            return StorageTierClass::kLocalFilesystem;  // never claim unproven NVMe
        default:
            return b.tier_class;
    }
}

// Builds deterministic synthetic tier metadata for an unavailable remote tier.
TierMetadata synthetic_tier_metadata(StorageTierId id, StorageTierClass cls,
                                     std::uint64_t total_bytes,
                                     std::uint64_t latency_us) {
    TierMetadata m;
    m.id = id;
    m.tier_class = cls;
    m.total_bytes = total_bytes;
    m.free_bytes = total_bytes;  // synthetic: assume empty until objects are placed
    m.latency_us = latency_us;
    m.durability = DurabilityClass::kUnknown;
    m.provenance = Provenance::kSynthetic;
    m.freshness = Freshness::kCurrent;
    m.locality = "synthetic-remote";
    m.failure_domain = "synthetic-fd";
    m.cost_class = "synthetic";
    m.health = true;
    return m;
}

}  // namespace checkpointstore
