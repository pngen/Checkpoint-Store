#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/base/error.hpp>

#include <filesystem>
#include <fstream>

using namespace checkpointstore;

static Bytes pat(char c, std::size_t n){ Bytes b(n); for(auto& x:b) x=Byte(c); return b; }

int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_test_adversarial";
    std::filesystem::remove_all(root);
    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(1); o.chunk_size = 256 * 1024;
    CheckpointStore s(o);
    s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    auto fam = s.create_family(OwnerId(1));

    // Zero-length checkpoint publishes with zero chunks (valid edge).
    {
        auto dz = s.make_full_descriptor(fam, OwnerId(1), 0);
        auto pubz = s.publish(dz, ByteView());
        CHECK_EQ(pubz.manifest.chunks.size(), 0u);
        CHECK_EQ(pubz.manifest.logical_size, 0u);
        auto rcz = s.restore(RestoreId(500), dz.id);
        CHECK(rcz.bytes.empty());
    }

    // Invalid chunk size config rejected on publish.
    {
        StoreOptions bad = o; bad.chunk_size = 0; bad.state_path = root / "bad";
        CheckpointStore sb(bad);
        sb.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
        auto f = sb.create_family(OwnerId(1));
        Bytes data = pat('Q', 100 * 1024);
        auto d = sb.make_full_descriptor(f, OwnerId(1), data.size());
        bool threw = false;
        try { auto p = sb.publish(d, ByteView(data.data(), data.size())); (void)p; }
        catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kInvalidArgument; }
        CHECK(threw);
    }

    // Duplicate commit / duplicate checkpoint id rejected.
    {
        Bytes data = pat('D', 100 * 1024);
        auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
        s.publish(d, ByteView(data.data(), data.size()));
        bool threw = false;
        try { auto p = s.publish(d, ByteView(data.data(), data.size())); (void)p; }
        catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kAlreadyExists; }
        CHECK(threw);
    }

    // Stale generation and stale authority are rejected.
    {
        Bytes data = pat('E', 100 * 1024);
        auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
        d.generation = CheckpointGeneration(1);   // stale (already used by a previous family checkpoint? generation is per-family)
        // generations per family: family has 2 checkpoints now, so next is 3; using 1 is stale.
        bool threw = false;
        try { auto p = s.publish(d, ByteView(data.data(), data.size())); (void)p; }
        catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kStaleGeneration; }
        CHECK(threw);
    }

    // Completion-after-failure never commits: an interrupted publish (stale
    // boot) leaves no committed checkpoint.
    {
        Bytes data = pat('F', 300 * 1024);
        auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
        d.producer_boot = WorkerBootId(9999);   // stale boot
        bool threw = false;
        try { auto p = s.publish(d, ByteView(data.data(), data.size())); (void)p; }
        catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kStaleAuthority; }
        CHECK(threw);
        CHECK(!s.exists(d.id));   // never committed
    }

    // Digest mismatch / sole-source corruption never becomes VERIFIED.
    {
        Bytes data; for (char c='X'; c<='Y'; ++c){auto p=pat(c,256*1024); data.insert(data.end(),p.begin(),p.end());}
        auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
        auto pub = s.publish(d, ByteView(data.data(), data.size()));
        auto chunks = s.get_chunks(d.id);
        auto digest = chunks.front().digest;
        std::string blob_path = (root / "store" / "blobs" / crypto::hex(digest).substr(0,2) / crypto::hex(digest)).string();
        std::ofstream out(blob_path, std::ios::binary | std::ios::trunc); out << "corrupt"; out.close();
        CHECK(s.verify_checkpoint(d.id) == IntegrityState::kCorrupt);
        bool ok = false;
        try { auto rc = s.restore(RestoreId(600), d.id); (void)rc; }
        catch (const CheckpointStoreError& e) { ok = e.code() == ErrorCode::kCorrupt; }
        CHECK(ok);   // refused corrupt sole source
    }

    std::filesystem::remove_all(root);
    return cpstest::finish("test_adversarial");
}
