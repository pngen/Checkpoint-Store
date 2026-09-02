#include <checkpointstore/storage/synthetic_backend.hpp>

#include <checkpointstore/base/error.hpp>

#include <limits>
#include <utility>

namespace checkpointstore {

SyntheticBackend::SyntheticBackend(StorageTierClass tier_class, StorageTierId tier_id,
                                   StorageBackendId id, std::string name)
    : SyntheticBackend(tier_class, tier_id, id, std::move(name), StorageNodeId(1),
                       VolumeId(1), std::numeric_limits<std::size_t>::max()) {}

SyntheticBackend::SyntheticBackend(StorageTierClass tier_class, StorageTierId tier_id,
                                   StorageBackendId id, std::string name,
                                   StorageNodeId node_id, VolumeId volume_id,
                                   std::size_t capacity_bytes)
    : tier_class_(tier_class), capacity_bytes_(capacity_bytes) {
    capabilities_.set(BackendCapability::kPersistent);
    capabilities_.set(BackendCapability::kRandomRead);
    capabilities_.set(BackendCapability::kRandomWrite);
    capabilities_.set(BackendCapability::kRangeRead);
    capabilities_.set(BackendCapability::kMultipart);
    capabilities_.set(BackendCapability::kServerSideCopy);
    capabilities_.set(BackendCapability::kObjectVersioning);
    capabilities_.set(BackendCapability::kDeletion);
    capabilities_.set(BackendCapability::kLocalStaging);

    descriptor_.id = id;
    descriptor_.generation = BackendGeneration(1);
    descriptor_.name = std::move(name);
    descriptor_.tier_class = tier_class;
    descriptor_.tier_id = tier_id;
    descriptor_.node_id = node_id;
    descriptor_.volume_id = volume_id;
    descriptor_.capabilities = capabilities_;
    descriptor_.provenance = Provenance::kSynthetic;
    descriptor_.freshness = Freshness::kCurrent;
    descriptor_.root_description = "synthetic (in-memory): " + descriptor_.name;
}

const BackendDescriptor& SyntheticBackend::descriptor() const { return descriptor_; }
const BackendCapabilities& SyntheticBackend::capabilities() const { return capabilities_; }

TierMetadata SyntheticBackend::query_capacity() {
    std::lock_guard<std::mutex> lock(mutex_);
    TierMetadata meta;
    meta.id = descriptor_.tier_id;
    meta.tier_class = tier_class_;
    meta.total_bytes = capacity_bytes_ == std::numeric_limits<std::size_t>::max()
                           ? 0
                           : static_cast<std::uint64_t>(capacity_bytes_);
    std::uint64_t used = 0;
    for (const auto& [key, data] : objects_) {
        (void)key;
        used += static_cast<std::uint64_t>(data.size());
    }
    meta.free_bytes = (capacity_bytes_ == std::numeric_limits<std::size_t>::max())
                          ? 0
                          : static_cast<std::uint64_t>(capacity_bytes_) - used;
    meta.latency_us = latency_us_;
    meta.durability = DurabilityClass::kUnknown;
    meta.provenance = Provenance::kSynthetic;
    meta.freshness = Freshness::kCurrent;
    meta.health = available_;
    meta.locality = "synthetic-remote";
    meta.failure_domain = "fd-" + std::to_string(descriptor_.node_id.value());
    meta.cost_class = "synthetic";
    return meta;
}

void SyntheticBackend::put_temp(BackendKey key, ByteView data) {
    if (!validate_backend_key(key.value)) {
        throw_error(ErrorCode::kInvalidArgument, "invalid backend key");
    }
    if (!available_) {
        throw_error(ErrorCode::kBackendUnavailable, "synthetic backend unavailable");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string temp = "temp/" + key.value + "." + std::to_string(++serial_);
    temp_objects_[temp] = Bytes(data.begin(), data.end());
    staged_[key.value] = temp;
}

void SyntheticBackend::commit(BackendKey key) {
    std::string temp;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = staged_.find(key.value);
        if (it == staged_.end()) {
            throw_error(ErrorCode::kTransaction, "commit called for unstaged key");
        }
        temp = it->second;
    }
    if (!available_) {
        throw_error(ErrorCode::kBackendUnavailable, "synthetic backend unavailable");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto t = temp_objects_.find(temp);
    if (t == temp_objects_.end()) {
        throw_error(ErrorCode::kTransaction, "staged temp missing");
    }
    objects_[key.value] = t->second;
    temp_objects_.erase(t);
    staged_.erase(key.value);
    corrupt_.erase(key.value);
    missing_.erase(key.value);
}

void SyntheticBackend::commit(BackendKey temp_key, BackendKey final_key) {
    if (!available_) {
        throw_error(ErrorCode::kBackendUnavailable, "synthetic backend unavailable");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    auto t = temp_objects_.find(temp_key.value);
    if (t == temp_objects_.end()) {
        throw_error(ErrorCode::kTransaction, "staged temp missing");
    }
    objects_[final_key.value] = t->second;
    temp_objects_.erase(t);
    corrupt_.erase(final_key.value);
    missing_.erase(final_key.value);
}

Bytes SyntheticBackend::read(BackendKey key) {
    if (!validate_backend_key(key.value)) {
        throw_error(ErrorCode::kInvalidArgument, "invalid backend key");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_) {
        throw_error(ErrorCode::kBackendUnavailable, "synthetic backend unavailable");
    }
    if (missing_.count(key.value)) {
        throw_error(ErrorCode::kMissing, "synthetic object missing");
    }
    auto it = objects_.find(key.value);
    if (it == objects_.end()) {
        throw_error(ErrorCode::kNotFound, "synthetic object not found");
    }
    return it->second;
}

bool SyntheticBackend::exists(BackendKey key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (missing_.count(key.value)) {
        return false;
    }
    return objects_.count(key.value) != 0;
}

bool SyntheticBackend::remove(BackendKey key) {
    std::lock_guard<std::mutex> lock(mutex_);
    return objects_.erase(key.value) != 0;
}

std::uint64_t SyntheticBackend::stat(BackendKey key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = objects_.find(key.value);
    return it == objects_.end() ? 0 : static_cast<std::uint64_t>(it->second.size());
}

void SyntheticBackend::flush() {}

std::vector<BackendKey> SyntheticBackend::list_controlled() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BackendKey> keys;
    keys.reserve(objects_.size());
    for (const auto& [key, data] : objects_) {
        (void)data;
        keys.push_back(BackendKey{key});
    }
    return keys;
}

IntegrityState SyntheticBackend::verify(BackendKey key, const crypto::Sha256Digest& expected) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!available_) {
        return IntegrityState::kUnknown;
    }
    if (missing_.count(key.value)) {
        return IntegrityState::kMissing;
    }
    auto it = objects_.find(key.value);
    if (it == objects_.end()) {
        return IntegrityState::kMissing;
    }
    if (corrupt_.count(key.value)) {
        return IntegrityState::kCorrupt;
    }
    const Bytes& data = it->second;
    auto digest = crypto::sha256(ByteView(data.data(), data.size()));
    return crypto::equal(digest, expected) ? IntegrityState::kVerified : IntegrityState::kCorrupt;
}

bool SyntheticBackend::health() { return available_; }

void SyntheticBackend::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    objects_.clear();
    temp_objects_.clear();
    staged_.clear();
    corrupt_.clear();
    missing_.clear();
}

void SyntheticBackend::set_capacity(std::size_t capacity_bytes) { capacity_bytes_ = capacity_bytes; }
void SyntheticBackend::set_available(bool available) { available_ = available; }
void SyntheticBackend::set_corrupt(BackendKey key) {
    std::lock_guard<std::mutex> lock(mutex_);
    corrupt_[key.value] = true;
}
void SyntheticBackend::set_missing(BackendKey key) {
    std::lock_guard<std::mutex> lock(mutex_);
    missing_[key.value] = true;
}
void SyntheticBackend::set_latency_us(std::uint64_t latency_us) { latency_us_ = latency_us; }

}  // namespace checkpointstore
