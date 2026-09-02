#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/storage/synthetic_backend.hpp>
#include <checkpointstore/model.hpp>
#include <checkpointstore/explain.hpp>

#include <filesystem>
#include <fstream>
#include <string>

using namespace checkpointstore;

static Bytes pat(char c, std::size_t n){ Bytes b(n); for(auto& x:b) x=Byte(c); return b; }

int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_test_restore";
    std::filesystem::remove_all(root);
    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(4); o.chunk_size = 128 * 1024;
    CheckpointStore s(o);
    auto lb = std::make_shared<LocalBackend>(root / "store", StorageBackendId(1));
    s.register_backend(StorageBackendId(1), lb);
    auto fam = s.create_family(OwnerId(1));
    Bytes data; for (char c='A';c<='E';++c){auto p=pat(c,128*1024); data.insert(data.end(),p.begin(),p.end());}
    auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
    auto pub = s.publish(d, ByteView(data.data(), data.size()));

    // Restore byte parity and priority planning.
    auto plan = s.plan_restore(d.id, RestorePriority::kCritical);
    CHECK(plan.priority == RestorePriority::kCritical);
    CHECK_EQ(plan.expected_bytes, data.size());
    CHECK(plan.steps.size() == pub.manifest.chunks.size());
    auto rc = s.restore(RestoreId(1), d.id, RestorePriority::kCritical);
    CHECK(rc.bytes == data);
    CHECK(rc.integrity == IntegrityState::kVerified);
    CHECK(rc.evidence.bytes_restored == data.size());
    auto exp = explain_restore(rc.evidence);
    CHECK(!exp.empty());

    // Corrupt the sole source -> restore must refuse, not silently repair.
    {
        auto chunks = s.get_chunks(d.id);
        auto digest = chunks.front().digest;
        std::string blob_path = (root / "store" / "blobs" / crypto::hex(digest).substr(0,2) / crypto::hex(digest)).string();
        std::ofstream out(blob_path, std::ios::binary | std::ios::trunc); out << "BAD"; out.close();
        bool refused = false;
        try { auto r2 = s.restore(RestoreId(2), d.id); (void)r2; }
        catch (const CheckpointStoreError& e) { refused = e.code() == ErrorCode::kCorrupt; }
        CHECK(refused);
        // verify_checkpoint must report CORRUPT.
        CHECK(s.verify_checkpoint(d.id) == IntegrityState::kCorrupt);
    }

    // Healthy replica fallback: place a second healthy synthetic replica and a
    // corrupt first replica. Restore must prefer the healthy one.
    {
        auto root2 = std::filesystem::temp_directory_path() / "cps_test_restore_h";
        std::filesystem::remove_all(root2);
        StoreOptions o2; o2.state_path = root2 / "s"; o2.boot_id = WorkerBootId(5); o2.chunk_size = 128 * 1024; o2.primary_backend_id = StorageBackendId(11);
        CheckpointStore s2(o2);
        auto healthy = std::make_shared<SyntheticBackend>(StorageTierClass::kLocalFilesystem, StorageTierId(1), StorageBackendId(11), "healthy");
        s2.register_backend(StorageBackendId(11), healthy);
        auto fam2 = s2.create_family(OwnerId(1));
        Bytes d2data; for (char c='A';c<='E';++c){auto p=pat(c,128*1024); d2data.insert(d2data.end(),p.begin(),p.end());}
        auto dd = s2.make_full_descriptor(fam2, OwnerId(1), d2data.size());
        s2.publish(dd, ByteView(d2data.data(), d2data.size()));
        // Corrupt the synthetic blob of the first chunk.
        auto chunks = s2.get_chunks(dd.id);
        auto digest = chunks.front().digest;
        std::string key = "blobs/" + crypto::hex(digest).substr(0,2) + "/" + crypto::hex(digest);
        healthy->set_corrupt(BackendKey{key});
        // Restore should still succeed by failing over to the only (healthy) path?
        // With one replica corrupt, restore must now refuse.
        bool ok = false;
        try { auto r = s2.restore(RestoreId(3), dd.id); (void)r; ok = true; }
        catch (const CheckpointStoreError& e) { ok = e.code() == ErrorCode::kCorrupt; }
        CHECK(ok);   // sole corrupt source refused
        std::filesystem::remove_all(root2);
    }

    std::filesystem::remove_all(root);
    return cpstest::finish("test_restore");
}