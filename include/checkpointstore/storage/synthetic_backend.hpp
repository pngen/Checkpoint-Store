#ifndef CHECKPOINTSTORE_STORAGE_SYNTHETIC_BACKEND_HPP
#define CHECKPOINTSTORE_STORAGE_SYNTHETIC_BACKEND_HPP

#include <checkpointstore/storage/backend.hpp>

#include <map>
#include <mutex>
#include <string>

namespace checkpointstore {

// A deterministic in-memory synthetic backend that models an unavailable
// remote or object tier. It is labeled provenance=SYNTHETIC and never claims a
// physical durability class. Object versioning, server-side copy, and
// multipart staging are modeled so the store can exercise tier-diverse
// placement logic without real cloud infrastructure. It is process-local and
// not durable across restart.
class SyntheticBackend : public IBackend {
public:
    explicit SyntheticBackend(StorageTierClass tier_class, StorageTierId tier_id,
                              StorageBackendId id, std::string name);
    SyntheticBackend(StorageTierClass tier_class, StorageTierId tier_id,
                     StorageBackendId id, std::string name,
                     StorageNodeId node_id, VolumeId volume_id, std::size_t capacity_bytes);

    [[nodiscard]] const BackendDescriptor& descriptor() const override;
    [[nodiscard]] const BackendCapabilities& capabilities() const override;
    [[nodiscard]] TierMetadata query_capacity() override;
    void put_temp(BackendKey key, ByteView data) override;
    void commit(BackendKey key) override;
    void commit(BackendKey temp_key, BackendKey final_key) override;
    [[nodiscard]] Bytes read(BackendKey key) override;
    [[nodiscard]] bool exists(BackendKey key) override;
    bool remove(BackendKey key) override;
    [[nodiscard]] std::uint64_t stat(BackendKey key) override;
    void flush() override;
    [[nodiscard]] std::vector<BackendKey> list_controlled() override;
    [[nodiscard]] IntegrityState verify(BackendKey key, const crypto::Sha256Digest& expected) override;
    [[nodiscard]] bool health() override;
    void close() override;

    // For deterministic scenario control.
    void set_capacity(std::size_t capacity_bytes);
    void set_available(bool available);
    void set_corrupt(BackendKey key);
    void set_missing(BackendKey key);
    void set_latency_us(std::uint64_t latency_us);

private:
    BackendDescriptor descriptor_;
    BackendCapabilities capabilities_;
    StorageTierClass tier_class_;
    std::size_t capacity_bytes_;
    std::uint64_t latency_us_ = 0;
    std::size_t serial_ = 0;
    bool available_ = true;
    mutable std::mutex mutex_;
    std::map<std::string, Bytes> objects_;   // final keys
    std::map<std::string, std::string> staged_;  // final key -> temp key
    std::map<std::string, Bytes> temp_objects_;
    std::map<std::string, bool> corrupt_;
    std::map<std::string, bool> missing_;
};

}  // namespace checkpointstore

#endif
