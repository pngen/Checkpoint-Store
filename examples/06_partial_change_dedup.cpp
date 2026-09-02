#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex06");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes base = cpstest_ex::pattern('A', 64 * 1024);
    auto d1 = store.make_full_descriptor(fam, OwnerId(1), base.size());
    store.publish(d1, ByteView(base.data(), base.size()));
    // Partial change: first half same, second half different.
    Bytes changed = base;
    Bytes suffix = cpstest_ex::pattern('Z', 32 * 1024);
    for (std::size_t i = 0; i < suffix.size(); ++i) changed[32 * 1024 + i] = suffix[i];
    auto d2 = store.make_full_descriptor(fam, OwnerId(1), changed.size());
    auto p2 = store.publish(d2, ByteView(changed.data(), changed.size()));
    std::cout << "partial-change publish hits=" << p2.dedup_hits << " misses=" << p2.dedup_misses
              << " (reused changed region, wrote only new blocks)\n";
    return 0;
}
