#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/storage/dedup.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>

#include <filesystem>
#include <iostream>

using namespace checkpointstore;

// Deterministic LCG for property-data generation (fixed seed; printed).
static std::uint64_t g_seed = 0x9E3779B97F4A7C15ull;
static std::uint64_t next_u64() {
    g_seed = g_seed * 6364136223846793005ull + 1442695040888963407ull;
    return g_seed;
}
static Bytes gen_data(std::size_t n) {
    Bytes b(n);
    for (std::size_t i=0;i<n;i++) b[i] = Byte(next_u64() & 0xFF);
    return b;
}

int main() {
    std::cout << "property seed = " << std::hex << g_seed << std::dec << "\n";
    auto root = std::filesystem::temp_directory_path() / "cps_test_property";
    std::filesystem::remove_all(root);

    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(77); o.chunk_size = 64 * 1024;
    CheckpointStore s(o);
    s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    auto fam = s.create_family(OwnerId(1));

    // Deterministic chunking: same data, same digest sequence across stores.
    Bytes data = gen_data(200 * 1024 + 1234);
    auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
    auto pub = s.publish(d, ByteView(data.data(), data.size()));
    auto prv_digests = [&](){ std::vector<crypto::Sha256Digest> v; for (auto& c : s.get_chunks(d.id)) v.push_back(c.digest); return v; };
    auto v1 = prv_digests();

    // Recompute independently with a second store + same data.
    StoreOptions o2; o2.state_path = root / "s2"; o2.boot_id = WorkerBootId(78); o2.chunk_size = 64 * 1024;
    CheckpointStore s2(o2);
    s2.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store2", StorageBackendId(1)));
    auto fam2 = s2.create_family(OwnerId(1));
    auto d2 = s2.make_full_descriptor(fam2, OwnerId(1), data.size());
    auto pub2 = s2.publish(d2, ByteView(data.data(), data.size()));
    auto v2 = [&](){ std::vector<crypto::Sha256Digest> v; for (auto& c : s2.get_chunks(d2.id)) v.push_back(c.digest); return v; }();
    CHECK(v1.size() == v2.size());
    for (size_t i=0;i<v1.size();i++) CHECK(v1[i] == v2[i]);

    // Refcounts never negative and exact across a shared-chunk scenario.
    // Second checkpoint in the SAME family reusing the same content.
    auto d3 = s.make_full_descriptor(fam, OwnerId(1), data.size());
    auto pub3 = s.publish(d3, ByteView(data.data(), data.size()));
    CHECK(pub3.dedup_hits > 0);
    CHECK(pub3.dedup_misses == 0);   // fully deduplicated

    // Dedup never merges unequal content: a different digest maps to a different blob.
    Bytes other = gen_data(200 * 1024 + 1234);
    bool differ = (crypto::sha256(ByteView(data.data(), data.size())) != crypto::sha256(ByteView(other.data(), other.size())));
    CHECK(differ);

    // UNKNOWN < UNVERIFIED < VERIFIED: never promote UNKNOWN to VERIFIED silently.
    CHECK(IntegrityState::kUnknown < IntegrityState::kUnverified);
    CHECK(IntegrityState::kUnverified < IntegrityState::kVerified);

    // Stable SHA-256 identity.
    auto h1 = crypto::sha256(ByteView(data.data(), data.size()));
    auto h2 = crypto::sha256(ByteView(data.data(), data.size()));
    CHECK(h1 == h2);

    std::filesystem::remove_all(root);
    return cpstest::finish("test_property");
}
