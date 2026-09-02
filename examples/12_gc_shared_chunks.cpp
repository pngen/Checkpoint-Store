#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex12");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes base = cpstest_ex::pattern('A', 64 * 1024);
    auto d1 = store.make_full_descriptor(fam, OwnerId(1), base.size());
    store.publish(d1, ByteView(base.data(), base.size()));
    Bytes d2d = base; Bytes g = cpstest_ex::pattern('G', 32 * 1024); d2d.insert(d2d.end(), g.begin(), g.end());
    auto d2 = store.make_full_descriptor(fam, OwnerId(1), d2d.size());
    store.publish(d2, ByteView(d2d.data(), d2d.size()));
    store.retire(d1.id);
    auto plan = store.gc_plan(); auto res = store.gc_run();
    auto rc = store.restore(RestoreId(3), d2.id);
    std::cout << "after retiring shared checkpoint 1, checkpoint 2 restores parity=" << (rc.bytes == d2d)
              << " reclaimed=" << res.reclaimed_blobs.size() << "\n";
    store.retire(d2.id);
    auto plan2 = store.gc_plan(); auto res2 = store.gc_run();
    std::cout << "after retiring all: reclaimed=" << res2.reclaimed_blobs.size() << "\n";
    return 0;
}
