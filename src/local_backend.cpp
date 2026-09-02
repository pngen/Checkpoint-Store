#include <checkpointstore/storage/local_backend.hpp>

#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace checkpointstore {

namespace {

// Fsync a file handle to ensure committed content is durable where supported.
void fsync_path(const std::filesystem::path& p) {
#ifdef _WIN32
    HANDLE h = ::CreateFileW(p.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        ::FlushFileBuffers(h);
        ::CloseHandle(h);
    }
#else
    (void)p;
#endif
}

std::string flatten_key(std::string_view key) {
    std::string out;
    out.reserve(key.size());
    for (char c : key) {
        out.push_back(c == '/' ? '_' : c);
    }
    return out;
}

}  // namespace

LocalBackend::LocalBackend(std::filesystem::path root, StorageBackendId id) : root_(std::move(root)) {
    if (root_.empty()) {
        throw_error(ErrorCode::kInvalidArgument, "local backend root is empty");
    }
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    if (ec && !std::filesystem::exists(root_)) {
        throw_error(ErrorCode::kIo, "cannot create local backend root");
    }
    for (const char* sub : {"blobs", "temp", "quarantine", "manifests", "metadata"}) {
        std::filesystem::create_directories(root_ / sub, ec);
    }

    capabilities_.set(BackendCapability::kPersistent);
    capabilities_.set(BackendCapability::kAtomicRename);
    capabilities_.set(BackendCapability::kFsync);
    capabilities_.set(BackendCapability::kRandomRead);
    capabilities_.set(BackendCapability::kRandomWrite);
    capabilities_.set(BackendCapability::kRangeRead);
    capabilities_.set(BackendCapability::kDeletion);
    capabilities_.set(BackendCapability::kLocalStaging);
    capabilities_.set(BackendCapability::kDurabilityClass);

    descriptor_.id = id;
    descriptor_.generation = BackendGeneration(1);
    descriptor_.name = "local-filesystem";
    descriptor_.tier_class = StorageTierClass::kLocalFilesystem;
    descriptor_.tier_id = StorageTierId(1);
    descriptor_.node_id = StorageNodeId(1);
    descriptor_.volume_id = VolumeId(1);
    descriptor_.capabilities = capabilities_;
    descriptor_.provenance = Provenance::kMeasured;
    descriptor_.freshness = Freshness::kCurrent;
    descriptor_.root_description = root_.string();
}

const BackendDescriptor& LocalBackend::descriptor() const { return descriptor_; }
const BackendCapabilities& LocalBackend::capabilities() const { return capabilities_; }

TierMetadata LocalBackend::query_capacity() {
    TierMetadata meta;
    meta.id = descriptor_.tier_id;
    meta.tier_class = descriptor_.tier_class;
    std::error_code ec;
    const auto info = std::filesystem::space(root_, ec);
    if (!ec) {
        meta.total_bytes = info.capacity;
        meta.free_bytes = info.available;
        meta.provenance = Provenance::kMeasured;
        meta.freshness = Freshness::kCurrent;
    } else {
        meta.provenance = Provenance::kUnknown;
        meta.freshness = Freshness::kUnknown;
    }
    meta.durability = DurabilityClass::kLocal;
    meta.health = std::filesystem::is_directory(root_);
    return meta;
}

std::filesystem::path LocalBackend::physical_path(BackendKey key) const {
    if (!validate_backend_key(key.value)) {
        throw_error(ErrorCode::kInvalidArgument, "invalid backend key");
    }
    // std::filesystem::path accepts forward slashes on every platform; keep
    // the governed key as a relative multi-component path inside the root.
    std::filesystem::path p = root_;
    p /= std::filesystem::path(key.value);
    return p;
}

std::filesystem::path LocalBackend::temp_path(BackendKey key) {
    if (!validate_backend_key(key.value)) {
        throw_error(ErrorCode::kInvalidArgument, "invalid backend key");
    }
    const std::uint64_t seq = ++seq_;
    return root_ / "temp" /
           (flatten_key(key.value) + "_" + std::to_string(seq) + ".tmp");
}

void LocalBackend::put_temp(BackendKey key, ByteView data) {
    const auto final_path = physical_path(key);
    const auto tmp_path = temp_path(key);
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw_error(ErrorCode::kIo, "cannot open temp for write");
        }
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out.good()) {
            throw_error(ErrorCode::kIo, "temp write failed");
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    staged_[key.value] = tmp_path.string();
    (void)final_path;
}

void LocalBackend::commit(BackendKey key) {
    std::string tmp;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = staged_.find(key.value);
        if (it == staged_.end()) {
            throw_error(ErrorCode::kTransaction, "commit called for unstaged key");
        }
        tmp = it->second;
    }
    const auto final_path = physical_path(key);
    std::error_code ec;
    if (std::filesystem::exists(final_path, ec)) {
        std::filesystem::remove(final_path, ec);
    }
    std::filesystem::create_directories(final_path.parent_path(), ec);
    std::filesystem::rename(tmp, final_path, ec);
    if (ec) {
        throw_error(ErrorCode::kIo, "commit rename failed");
    }
    fsync_path(final_path);
    std::lock_guard<std::mutex> lock(mutex_);
    staged_.erase(key.value);
}

void LocalBackend::commit(BackendKey temp_key, BackendKey final_key) {
    const auto tmp_path = physical_path(temp_key);
    const auto final_path = physical_path(final_key);
    std::error_code ec;
    if (std::filesystem::exists(final_path, ec)) {
        std::filesystem::remove(final_path, ec);
    }
    std::filesystem::create_directories(final_path.parent_path(), ec);
    std::filesystem::rename(tmp_path, final_path, ec);
    if (ec) {
        throw_error(ErrorCode::kIo, "commit rename failed");
    }
    fsync_path(final_path);
}

Bytes LocalBackend::read(BackendKey key) {
    const auto p = physical_path(key);
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        throw_error(ErrorCode::kNotFound, "backend object not found");
    }
    std::vector<std::byte> data;
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size > 0) {
        data.resize(static_cast<std::size_t>(size));
        in.seekg(0, std::ios::beg);
        in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
    }
    return data;
}

bool LocalBackend::exists(BackendKey key) {
    std::error_code ec;
    return std::filesystem::exists(physical_path(key), ec);
}

bool LocalBackend::remove(BackendKey key) {
    std::error_code ec;
    return std::filesystem::remove(physical_path(key), ec);
}

std::uint64_t LocalBackend::stat(BackendKey key) {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(physical_path(key), ec);
    if (ec) {
        return 0;
    }
    return static_cast<std::uint64_t>(sz);
}

void LocalBackend::flush() {
    // Local filesystem: committed content is already fsync'd at commit time.
    // Flush is a no-op contractually for durability.
}

std::vector<BackendKey> LocalBackend::list_controlled() {
    std::vector<BackendKey> keys;
    std::error_code ec;
    for (const char* sub : {"blobs", "manifests"}) {
        const auto base = root_ / sub;
        if (!std::filesystem::is_directory(base, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::recursive_directory_iterator(base, ec)) {
            if (entry.is_regular_file(ec)) {
                auto rel = std::filesystem::relative(entry.path(), root_, ec);
                const std::string rel_str = rel.generic_string();
                keys.push_back(BackendKey{rel_str});
            }
        }
    }
    return keys;
}

IntegrityState LocalBackend::verify(BackendKey key, const crypto::Sha256Digest& expected) {
    const auto p = physical_path(key);
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        return IntegrityState::kMissing;
    }
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        return IntegrityState::kMissing;
    }
    crypto::Sha256Hasher hasher;
    std::array<std::byte, 1 << 16> buf{};
    while (in) {
        in.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
        const auto got = in.gcount();
        if (got > 0) {
            hasher.update(ByteView(buf.data(), static_cast<std::size_t>(got)));
        }
    }
    auto digest = hasher.final();
    return crypto::equal(digest, expected) ? IntegrityState::kVerified : IntegrityState::kCorrupt;
}

bool LocalBackend::health() {
    std::error_code ec;
    return std::filesystem::is_directory(root_, ec);
}

void LocalBackend::close() {
    // Nothing persistent to release; temp staging is process-local.
}

}  // namespace checkpointstore
