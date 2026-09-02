#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex14");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes data = cpstest_ex::pattern('A', 80 * 1024);
    auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
    store.publish(d, ByteView(data.data(), data.size()));
    // A stale incarnation (boot 99) must never publish a fresh generation.
    auto stale = cpstest_ex::make_store(root, WorkerBootId(99), 32 * 1024);
    auto fam2 = stale.create_family(OwnerId(1));
    auto d2 = stale.make_full_descriptor(fam2, OwnerId(1), data.size());
    d2.producer_boot = WorkerBootId(1);   // stale
    try { stale.publish(d2, ByteView(data.data(), data.size())); }
    catch (const CheckpointStoreError& e) {
        std::cout << "stale authority rejected: " << to_string(e.code()) << "\n";
    }
    return 0;
}
