#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/storage/synthetic_backend.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/protocol/protocol.hpp>

#include <iostream>
#include <filesystem>
#include <string>

using namespace checkpointstore;

static Bytes make_pattern(char fill, std::size_t n) {
    Bytes b(n);
    for (auto& x : b) x = static_cast<Byte>(fill);
    return b;
}

int main() {
    int failed = 0;
    auto check = [&](bool cond, const char* msg) {
        if (!cond) { std::cout << "FAIL: " << msg << "\n"; ++failed; }
    };

    std::filesystem::path root = std::filesystem::temp_directory_path() / "cps_core_smoke";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    StoreOptions opts;
    opts.state_path = root / "metadata" / "checkpoint-store.state";
    opts.boot_id = WorkerBootId(42);
    opts.chunk_size = 512 * 1024;
    opts.required_replica_count = 1;
    CheckpointStore store(opts);
    auto lb = std::make_shared<LocalBackend>(root / "store", StorageBackendId(1));
    store.register_backend(StorageBackendId(1), lb);
    store.register_backend(StorageBackendId(9), std::make_shared<SyntheticBackend>(
        StorageTierClass::kSyntheticRemote, StorageTierId(2), StorageBackendId(9), "syn"));

    auto fam = store.create_family(OwnerId(1));
    Bytes data1;
    for (char ch = 'A'; ch <= 'F'; ++ch) {
        Bytes c = make_pattern(ch, 512 * 1024);
        data1.insert(data1.end(), c.begin(), c.end());
    }
    auto d1 = store.make_full_descriptor(fam, OwnerId(1), data1.size());
    auto pub1 = store.publish(d1, ByteView(data1.data(), data1.size()));
    check(pub1.descriptor.id.value() == d1.id.value(), "published checkpoint 1 id");
    check(store.lifecycle(d1.id) == CheckpointLifecycle::kCommitted, "checkpoint 1 committed");
    check(pub1.dedup_hits == 0 && pub1.dedup_misses == 6, "checkpoint 1 all misses");
    auto iv1 = store.verify_checkpoint(d1.id);
    check(iv1 == IntegrityState::kVerified, "checkpoint 1 verified");

    Bytes data2 = data1;
    Bytes c3 = make_pattern('G', 512 * 1024);
    data2.insert(data2.end(), c3.begin(), c3.end());
    auto d2 = store.make_full_descriptor(fam, OwnerId(1), data2.size());
    auto pub2 = store.publish(d2, ByteView(data2.data(), data2.size()));
    check(pub2.dedup_hits >= 6, "checkpoint 2 dedup hits for shared chunks");
    auto iv2 = store.verify_checkpoint(d2.id);
    check(iv2 == IntegrityState::kVerified, "checkpoint 2 verified");

    auto rc = store.restore(RestoreId(1), d2.id, RestorePriority::kNormal);
    check(rc.bytes == data2, "restore byte parity");
    check(rc.integrity == IntegrityState::kVerified, "restore verified");

    store.save_state();
    CheckpointStore store2(opts);
    store2.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    store2.load_state();
    check(store2.exists(d1.id) && store2.exists(d2.id), "state round-trip preserves checkpoints");
    auto rc2 = store2.restore(RestoreId(2), d2.id);
    check(rc2.bytes == data2, "restore after reload byte parity");

    auto da = store2.dedup_accounting();
    check(da.unique_physical_bytes < da.logical_bytes, "dedup reduces physical bytes");
    check(da.logical_bytes == data1.size() + data2.size(), "logical bytes exact");

    store2.retire(d1.id);
    auto plan = store2.gc_plan();
    auto gcr = store2.gc_run();
    check(store2.lifecycle(d1.id) != CheckpointLifecycle::kCommitted, "retired checkpoint 1 is not committed");
    auto rc3 = store2.restore(RestoreId(3), d2.id);
    check(rc3.bytes == data2, "shared chunks survive GC after retiring checkpoint 1");

    store2.retire(d2.id);
    auto plan2 = store2.gc_plan();
    auto gcr2 = store2.gc_run();
    check(gcr2.reclaimed_blobs.size() > 0, "final GC reclaims orphaned blobs");

    auto acct = store2.accounting();
    std::cout << "accounting: checkpoints=" << acct.checkpoint_count
              << " committed=" << acct.committed_checkpoint_count
              << " blobs=" << acct.blob_count
              << " reclaimed_blobs=" << acct.reclaimed_blobs << "\n";

    Bytes payload = make_bytes("hello-frame");
    auto frame = protocol::encode_frame(protocol::MessageKind::kHeartbeat, ByteView(payload.data(), payload.size()));
    auto dec = protocol::decode_frame(ByteView(frame.data(), frame.size()));
    check(dec.kind == protocol::MessageKind::kHeartbeat, "protocol kind roundtrip");
    check(dec.payload == payload, "protocol payload roundtrip");

    std::filesystem::remove_all(root);
    std::cout << (failed == 0 ? "CORE SMOKE: PASS\n" : "CORE SMOKE: FAIL\n");
    return failed == 0 ? 0 : 1;
}
