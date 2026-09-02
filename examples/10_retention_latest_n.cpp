#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex10");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    for (char c : {'A','B','C'}) {
        Bytes data = cpstest_ex::pattern(c, 40 * 1024);
        auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
        store.publish(d, ByteView(data.data(), data.size()));
    }
    RetentionPolicy p; p.id = RetentionPolicyId(1); p.family_id = fam;
    p.retention_class = RetentionClass::kKeepLatestN; p.latest_n = 2;
    store.set_retention_policy(p);
    auto el = store.gc_eligible_checkpoints();
    std::cout << "latest_n=2 eligible=" << el.size() << " (oldest is GC-eligible)\n";
    return 0;
}
