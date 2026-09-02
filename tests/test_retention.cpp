#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/model.hpp>

#include <filesystem>
#include <chrono>
#include <algorithm>

using namespace checkpointstore;

static Bytes pat(char c, std::size_t n){ Bytes b(n); for(auto& x:b) x=Byte(c); return b; }

int main() {
    try {
    auto root = std::filesystem::temp_directory_path() / "cps_test_retention";
    std::filesystem::remove_all(root);
    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(6); o.chunk_size = 256 * 1024;
    CheckpointStore s(o);
    s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    auto fam = s.create_family(OwnerId(1));

    // Publish 3 independent checkpoints.
    auto pub3 = [&](char c) {
        Bytes data = pat(c, 300 * 1024);
        auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
        s.publish(d, ByteView(data.data(), data.size()));
        return d.id;
    };
    auto c1 = pub3('X');
    auto c2 = pub3('Y');
    auto c3 = pub3('Z');

    // latest-N = 2 protects the newest 2; oldest eligible.
    RetentionPolicy p; p.id = RetentionPolicyId(1); p.family_id = fam;
    p.retention_class = RetentionClass::kKeepLatestN; p.latest_n = 2;
    s.set_retention_policy(p);
    auto eligible = s.gc_eligible_checkpoints();
    CHECK(eligible.size() == 1);
    CHECK(eligible[0] == c1);
    CHECK(std::find(eligible.begin(), eligible.end(), c2) == eligible.end());
    CHECK(std::find(eligible.begin(), eligible.end(), c3) == eligible.end());

    // latest-N = 3 protects all.
    p.latest_n = 3;
    s.set_retention_policy(p);
    CHECK(s.gc_eligible_checkpoints().empty());

    // Pinned is never eligible.
    RetentionPolicy pin; pin.id = RetentionPolicyId(2); pin.family_id = fam;
    pin.retention_class = RetentionClass::kPinned; pin.policy_protected = true;
    s.set_retention_policy(pin);
    CHECK(s.gc_eligible_checkpoints().empty());

    // TTL: a checkpoint older than its TTL is eligible.
    {
        StoreOptions o2; o2.state_path = root / "s2"; o2.boot_id = WorkerBootId(8); o2.chunk_size = 256 * 1024;
        CheckpointStore s2(o2);
        s2.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store2", StorageBackendId(1)));
        auto fam2 = s2.create_family(OwnerId(1));
        Bytes data = pat('T', 300 * 1024);
        auto d = s2.make_full_descriptor(fam2, OwnerId(1), data.size());
        d.created_at = std::chrono::system_clock::now() - std::chrono::hours(2);
        d.retention = RetentionClass::kTtl;
        s2.publish(d, ByteView(data.data(), data.size()));
        RetentionPolicy pp; pp.id = RetentionPolicyId(3); pp.family_id = fam2;
        pp.retention_class = RetentionClass::kTtl; pp.ttl = std::chrono::seconds(3600);
        s2.set_retention_policy(pp);
        CHECK(s2.gc_eligible_checkpoints().size() == 1);
        std::filesystem::remove_all(root / "store2");
    }

    // Ancestry: child lineage protects parents even if they'd otherwise be
    // eligible under latest-N.
    {
        StoreOptions o3; o3.state_path = root / "s3"; o3.boot_id = WorkerBootId(9); o3.chunk_size = 256 * 1024;
        CheckpointStore s3(o3);
        s3.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store3", StorageBackendId(1)));
        auto fam3 = s3.create_family(OwnerId(1));
        Bytes dA = pat('A', 300 * 1024);
        auto da = s3.make_full_descriptor(fam3, OwnerId(1), dA.size());
        s3.publish(da, ByteView(dA.data(), dA.size()));
        Bytes dB = pat('B', 300 * 1024);
        auto db = s3.make_full_descriptor(fam3, OwnerId(1), dB.size());
        db.parent_checkpoint = da.id;
        db.lineage = {da.id};
        s3.publish(db, ByteView(dB.data(), dB.size()));
        RetentionPolicy p3; p3.id = RetentionPolicyId(4); p3.family_id = fam3;
        p3.retention_class = RetentionClass::kKeepLatestN; p3.latest_n = 1;
        s3.set_retention_policy(p3);
        // latest-n protects db (newest). Ancestry protects da.
        auto el = s3.gc_eligible_checkpoints();
        CHECK(el.empty());
        std::filesystem::remove_all(root / "store3");
    }

    std::filesystem::remove_all(root);
    return cpstest::finish("test_retention");
    } catch (const std::exception& e) { std::cerr << "EXC: " << e.what() << "\n"; return 1; }
}