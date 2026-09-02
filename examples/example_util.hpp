#ifndef CHECKPOINTSTORE_EXAMPLES_UTIL_HPP
#define CHECKPOINTSTORE_EXAMPLES_UTIL_HPP

#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/storage/synthetic_backend.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/explain.hpp>
#include <checkpointstore/tier.hpp>
#include <checkpointstore/replication.hpp>
#include <checkpointstore/base/error.hpp>

#include <filesystem>
#include <iostream>

namespace cpstest_ex {

using namespace checkpointstore;

inline std::filesystem::path temp_root(const char* name) {
    auto p = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p / "store");
    return p;
}

inline checkpointstore::Bytes pattern(char c, std::size_t n) {
    checkpointstore::Bytes b(n);
    for (auto& x : b) x = static_cast<checkpointstore::Byte>(c);
    return b;
}

inline checkpointstore::CheckpointStore make_store(const std::filesystem::path& root,
                                                   WorkerBootId boot = WorkerBootId(1),
                                                   std::uint64_t chunk = 64 * 1024) {
    checkpointstore::StoreOptions o;
    o.state_path = root / "metadata" / "checkpoint-store.state";
    o.boot_id = boot;
    o.chunk_size = chunk;
    checkpointstore::CheckpointStore s(o);
    s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    return s;
}

}  // namespace cpstest_ex
#endif
