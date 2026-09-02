#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/base/error.hpp>

#include <filesystem>

using namespace checkpointstore;

static Bytes pat(char c, std::size_t n){ Bytes b(n); for(auto& x:b) x=Byte(c); return b; }

int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_test_model";
    std::filesystem::remove_all(root);
    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(2); o.chunk_size = 256 * 1024;
    CheckpointStore s(o);
    s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    auto fam = s.create_family(OwnerId(1));
    CHECK(s.family_exists(fam));

    Bytes data = pat('A', 600 * 1024);
    auto d = s.make_full_descriptor(fam, OwnerId(1), data.size(), DurabilityClass::kReplicated);
    d.required_replica_count = 2;   // requires 2 replicas -> under-replicated initially
    auto pub = s.publish(d, ByteView(data.data(), data.size()));
    CHECK(s.lifecycle(d.id) == CheckpointLifecycle::kCommitted);
    CHECK(pub.descriptor.kind == CheckpointKind::kFull);
    auto replicas = s.get_replicas(d.id);
    CHECK_EQ(replicas.size(), 1u);   // single replica, requires 2 -> under-replicated
    CHECK(pub.descriptor.durability == DurabilityClass::kReplicated);

    // Incomplete / non-full semantics rejected (manifest-level only in 1.0.0).
    auto d2 = s.make_full_descriptor(fam, OwnerId(1), data.size());
    d2.kind = CheckpointKind::kIncremental;
    bool threw = false;
    try { auto p = s.publish(d2, ByteView(data.data(), data.size())); (void)p; }
    catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kNotSupported; }
    CHECK(threw);

    // Stale generation rejected.
    auto d3 = s.make_full_descriptor(fam, OwnerId(1), data.size());
    d3.generation = CheckpointGeneration(1);   // already used (stale)
    threw = false;
    try { auto p = s.publish(d3, ByteView(data.data(), data.size())); (void)p; }
    catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kStaleGeneration; }
    CHECK(threw);

    // Stale / mismatched producer boot rejected.
    auto d4 = s.make_full_descriptor(fam, OwnerId(1), data.size());
    d4.producer_boot = WorkerBootId(999);
    threw = false;
    try { auto p = s.publish(d4, ByteView(data.data(), data.size())); (void)p; }
    catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kStaleAuthority; }
    CHECK(threw);

    // Duplicate commit rejected.
    threw = false;
    try { auto p = s.publish(d, ByteView(data.data(), data.size())); (void)p; }
    catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kAlreadyExists; }
    CHECK(threw);

    // Queries.
    CHECK(s.exists(d.id));
    auto desc = s.get_checkpoint(d.id);
    CHECK_EQ(desc.logical_size, (std::uint64_t)data.size());
    auto chunks = s.get_chunks(d.id);
    CHECK_EQ(chunks.size(), 3u);   // 600KiB / 256KiB = 3 chunks (2 full + 1 partial)

    std::filesystem::remove_all(root);
    return cpstest::finish("test_checkpoint_model");
}
