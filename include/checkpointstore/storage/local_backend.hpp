#ifndef CHECKPOINTSTORE_STORAGE_LOCAL_BACKEND_HPP
#define CHECKPOINTSTORE_STORAGE_LOCAL_BACKEND_HPP

#include <checkpointstore/storage/backend.hpp>

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

namespace checkpointstore {

// A real filesystem backend rooted at a controlled directory. It stores blobs
// under root/blobs/<digest-prefix>/<digest>, stages writes under root/temp,
// and materializes manifests under root/manifests. All keys are governed
// relative paths and any traversal is rejected. Durability is provided by
// flushing staged content and fsync'ing committed files where the OS supports
// it. Physical-device classification is intentionally left UNKNOWN unless it
// can be established.
class LocalBackend : public IBackend {
public:
    // Creates a local backend rooted at the given directory. The directory is
    // created if it does not exist. Throws on a non-filesystem root.
    explicit LocalBackend(std::filesystem::path root, StorageBackendId id = StorageBackendId{1});

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

    // Root directory helpers for diagnostics.
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    [[nodiscard]] std::filesystem::path physical_path(BackendKey key) const;
    [[nodiscard]] std::filesystem::path temp_path(BackendKey key);

    std::filesystem::path root_;
    BackendDescriptor descriptor_;
    BackendCapabilities capabilities_;
    std::uint64_t seq_ = 0;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::string> staged_;  // final key -> temp abs path
};

}  // namespace checkpointstore

#endif
