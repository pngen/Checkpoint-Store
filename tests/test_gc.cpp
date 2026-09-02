#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/model.hpp>

#include <filesystem>

using namespace checkpointstore;

static Bytes pat(char c, std::size_t n){ Bytes b(n); for(auto& x:b) x=Byte(c); return b; }

int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_test_gc";
    std::filesystem::remove_all(root);
    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(10); o.chunk_size = 256 * 1024;
    CheckpointStore s(o);
    s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    auto fam = s.create_family(OwnerId(1));

    // Two checkpoints sharing chunks.
    Bytes base; for (char c='A'; c<='C'; ++c) { auto p=pat(c,256*1024); base.insert(base.end(), p.begin(), p.end()); }
    auto d1 = s.make_full_descriptor(fam, OwnerId(1), base.size());
    s.publish(d1, ByteView(base.data(), base.size()));

    Bytes d2data = base; auto g=pat('G',256*1024); d2data.insert(d2data.end(), g.begin(), g.end());
    auto d2 = s.make_full_descriptor(fam, OwnerId(1), d2data.size());
    s.publish(d2, ByteView(d2data.data(), d2data.size()));

    // Retire the first only; GC must preserve shared chunks (second still needs them).
    s.retire(d1.id);
    auto plan = s.gc_plan();
    auto r1 = s.gc_run();
    CHECK(s.exists(d2.id));
    CHECK(s.lifecycle(d2.id) == CheckpointLifecycle::kCommitted);
    auto rc = s.restore(RestoreId(20), d2.id, RestorePriority::kHigh);
    CHECK(rc.bytes == d2data);
    // d1's unique content (none unique here since d2 shares all base chunks) -> nothing reclaimed yet.
    // Retire the second; now all blobs unreferenced -> reclaimed.
    s.retire(d2.id);
    auto plan2 = s.gc_plan();
    auto r2 = s.gc_run();
    CHECK(r2.reclaimed_blobs.size() > 0);
    auto acct = s.accounting();
    CHECK_EQ(acct.reclaimed_blobs, r2.reclaimed_blobs.size());

    // Ancestry: parent explicitly retired but still needed by a retained child.
    {
        StoreOptions o4; o4.state_path = root / "s4"; o4.boot_id = WorkerBootId(11); o4.chunk_size = 256 * 1024;
        CheckpointStore s4(o4);
        s4.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store4", StorageBackendId(1)));
        auto f4 = s4.create_family(OwnerId(1));
        Bytes p = pat('P', 300 * 1024);
        auto dp = s4.make_full_descriptor(f4, OwnerId(1), p.size());
        s4.publish(dp, ByteView(p.data(), p.size()));
        Bytes c = pat('C', 300 * 1024);
        auto dc = s4.make_full_descriptor(f4, OwnerId(1), c.size());
        dc.parent_checkpoint = dp.id; dc.lineage = {dp.id};
        s4.publish(dc, ByteView(c.data(), c.size()));
        // Retire parent; lineage keeps the child protected by latest-N.
        s4.retire(dp.id);
        // Even though parent is retired, it is the ancestor of a retained child.
        auto el = s4.gc_eligible_checkpoints();
        CHECK(el.empty());   // ancestry protection wins over retire
        std::filesystem::remove_all(root / "store4");
    }

    std::filesystem::remove_all(root);
    return cpstest::finish("test_gc");
}
