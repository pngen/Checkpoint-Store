#ifndef CHECKPOINTSTORE_STORAGE_BACKEND_HPP
#define CHECKPOINTSTORE_STORAGE_BACKEND_HPP

#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/identity/generations.hpp>
#include <checkpointstore/identity/identities.hpp>
#include <checkpointstore/model.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace checkpointstore {

// A governed backend-relative storage key. Keys are always relative to a
// backend root; absolute paths, path traversal, NUL bytes, and reserved
// components are rejected. Network or protocol input can never influence an
// arbitrary absolute path.
struct BackendKey {
    std::string value;
};

// Validates a backend-relative key. Returns false for absolute paths, "..",
// embedded NUL, empty components after normalization, reserved path
// components, or malformed inputs.
[[nodiscard]] bool validate_backend_key(std::string_view key);

// Enumerates the capabilities a backend advertises. These are truthful
// declarations: do not claim a capability a backend cannot actually provide.
enum class BackendCapability : std::uint32_t {
    kPersistent = 1u << 0,
    kAtomicRename = 1u << 1,
    kFsync = 1u << 2,
    kRandomRead = 1u << 3,
    kRandomWrite = 1u << 4,
    kRangeRead = 1u << 5,
    kMultipart = 1u << 6,
    kServerSideCopy = 1u << 7,
    kObjectVersioning = 1u << 8,
    kDeletion = 1u << 9,
    kDurabilityClass = 1u << 10,
    kLocalStaging = 1u << 11,
};

struct BackendCapabilities {
    std::uint32_t flags = 0;

    void set(BackendCapability c) { flags |= static_cast<std::uint32_t>(c); }
    [[nodiscard]] bool has(BackendCapability c) const {
        return (flags & static_cast<std::uint32_t>(c)) != 0;
    }
    [[nodiscard]] bool persistent() const { return has(BackendCapability::kPersistent); }
    [[nodiscard]] bool atomic_rename() const { return has(BackendCapability::kAtomicRename); }
    [[nodiscard]] bool fsync() const { return has(BackendCapability::kFsync); }
    [[nodiscard]] bool random_read() const { return has(BackendCapability::kRandomRead); }
    [[nodiscard]] bool deletion() const { return has(BackendCapability::kDeletion); }
    [[nodiscard]] bool local_staging() const { return has(BackendCapability::kLocalStaging); }
    [[nodiscard]] bool durability_class() const { return has(BackendCapability::kDurabilityClass); }
};

struct BackendDescriptor {
    StorageBackendId id;
    BackendGeneration generation;
    std::string name;
    StorageTierClass tier_class = StorageTierClass::kUnknown;
    StorageTierId tier_id;
    StorageNodeId node_id;
    VolumeId volume_id;
    BackendCapabilities capabilities;
    Provenance provenance = Provenance::kUnknown;
    Freshness freshness = Freshness::kUnknown;
    std::string root_description;   // physical location, human readable
};

// The backend contract. Local filesystem and deterministic synthetic remote
// backends implement this interface. All mutating operations accept a governed
// relative key. Corruption of a stored object must be observable through
// verify()/read() rather than silently repaired.
class IBackend {
public:
    virtual ~IBackend() = default;

    [[nodiscard]] virtual const BackendDescriptor& descriptor() const = 0;
    [[nodiscard]] virtual const BackendCapabilities& capabilities() const = 0;

    [[nodiscard]] virtual TierMetadata query_capacity() = 0;

    // Writes an object into temporary/staging space that is not externally
    // visible until commit().
    virtual void put_temp(BackendKey key, ByteView data) = 0;

    // Atomically publishes a previously staged object, making it visible.
    virtual void commit(BackendKey key) = 0;
    // Two-phase: commit a staged temp object to a final key.
    virtual void commit(BackendKey temp_key, BackendKey final_key) = 0;

    [[nodiscard]] virtual Bytes read(BackendKey key) = 0;
    [[nodiscard]] virtual bool exists(BackendKey key) = 0;
    virtual bool remove(BackendKey key) = 0;
    [[nodiscard]] virtual std::uint64_t stat(BackendKey key) = 0;

    virtual void flush() = 0;

    // Lists governed keys currently visible under a logical namespace (for
    // example the blob namespace). Returned keys are backend-relative.
    [[nodiscard]] virtual std::vector<BackendKey> list_controlled() = 0;

    // Verifies a stored object against an expected SHA-256 digest. Returns the
    // resulting integrity state. A mismatch yields kCorrupt, never a repair.
    [[nodiscard]] virtual IntegrityState verify(BackendKey key, const crypto::Sha256Digest& expected) = 0;

    [[nodiscard]] virtual bool health() = 0;

    // Closes the backend and releases OS resources (releases/mutex/temp).
    virtual void close() = 0;
};

using BackendPtr = std::shared_ptr<IBackend>;

}  // namespace checkpointstore

#endif
