#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex09");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes data = cpstest_ex::pattern('P', 96 * 1024);
    auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
    store.publish(d, ByteView(data.data(), data.size()));
    auto plan = store.plan_restore(d.id, RestorePriority::kCritical);
    std::cout << "restore plan priority=" << to_string(plan.priority) << " steps=" << plan.steps.size()
              << " expected=" << plan.expected_bytes << "\n";
    auto rc = store.restore(RestoreId(2), d.id, RestorePriority::kHigh);
    std::cout << "restored integrity=" << to_string(rc.integrity) << " bytes=" << rc.bytes.size() << "\n";
    return 0;
}
