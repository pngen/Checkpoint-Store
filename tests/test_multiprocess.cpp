#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/base/error.hpp>

#include <filesystem>

using namespace checkpointstore;

static Bytes pat(char c, std::size_t n){ Bytes b(n); for(auto& x:b) x=Byte(c); return b; }

// Library-level authority fencing: a stale process incarnation (old WorkerBootId)
// can never publish or commit after a fresh incarnation takes authority. The
// real coordinator/worker TCP proof is exercised as a separate OS-process test.
int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_test_multiprocess";
    std::filesystem::remove_all(root);
    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(1); o.chunk_size = 128 * 1024;
    CheckpointStore s(o);
    s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    auto fam = s.create_family(OwnerId(1));
    Bytes data = pat('A', 300 * 1024);
    auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
    s.publish(d, ByteView(data.data(), data.size()));
    s.save_state();

    // A stale incarnation (boot 99) is rejected when it attempts to publish
    // under an authority it does not hold. A store whose boot is 99 must not
    // accept a descriptor produced by boot 1.
    {
        StoreOptions o_stale = o; o_stale.boot_id = WorkerBootId(99);
        CheckpointStore s2(o_stale);
        s2.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
        auto fam2 = s2.create_family(OwnerId(1));
        auto d2 = s2.make_full_descriptor(fam2, OwnerId(1), data.size());
        d2.producer_boot = WorkerBootId(1);   // stale incarnation's boot
        bool threw = false;
        try { s2.publish(d2, ByteView(data.data(), data.size())); }
        catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kStaleAuthority; }
        CHECK(threw);
        CHECK(!s2.exists(d2.id));
    }

    // Recovery: after coordinator restart, live authority must be cleared and a
    // fresh incarnation must be able to publish under current authority.
    {
        StoreOptions fr = o; fr.boot_id = WorkerBootId(100);   // fresh incarnation
        CheckpointStore fresh(fr);
        fresh.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
        fresh.load_state();
        fresh.reset_authority();
        CHECK(fresh.exists(d.id));
        auto d2 = fresh.make_full_descriptor(fam, OwnerId(1), data.size());
        auto pub = fresh.publish(d2, ByteView(data.data(), data.size()));
        CHECK(fresh.lifecycle(d2.id) == CheckpointLifecycle::kCommitted);
        CHECK(pub.dedup_hits > 0);   // shared with the earlier checkpoint
    }

    std::filesystem::remove_all(root);
    return cpstest::finish("test_multiprocess");
}
