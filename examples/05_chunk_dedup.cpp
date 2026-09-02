#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex05");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes data = cpstest_ex::pattern('E', 96 * 1024);
    auto d1 = store.make_full_descriptor(fam, OwnerId(1), data.size());
    auto p1 = store.publish(d1, ByteView(data.data(), data.size()));
    auto d2 = store.make_full_descriptor(fam, OwnerId(1), data.size());
    auto p2 = store.publish(d2, ByteView(data.data(), data.size()));
    std::cout << "second same-content publish hits=" << p2.dedup_hits << " misses=" << p2.dedup_misses << "\n";
    auto acct = store.dedup_accounting();
    std::cout << "logical=" << acct.logical_bytes << " unique=" << acct.unique_physical_bytes
              << " dedup=" << acct.deduplicated_bytes << "\n";
    return 0;
}
