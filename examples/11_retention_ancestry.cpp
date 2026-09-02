#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex11");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes da = cpstest_ex::pattern('A', 40 * 1024);
    auto a = store.make_full_descriptor(fam, OwnerId(1), da.size());
    store.publish(a, ByteView(da.data(), da.size()));
    Bytes db = cpstest_ex::pattern('B', 40 * 1024);
    auto b = store.make_full_descriptor(fam, OwnerId(1), db.size());
    b.parent_checkpoint = a.id; b.lineage = {a.id};
    store.publish(b, ByteView(db.data(), db.size()));
    RetentionPolicy p; p.id = RetentionPolicyId(1); p.family_id = fam;
    p.retention_class = RetentionClass::kKeepLatestN; p.latest_n = 1;
    store.set_retention_policy(p);
    auto el = store.gc_eligible_checkpoints();
    std::cout << "ancestry: eligible=" << el.size()
              << " (parent protected by retained child lineage)\n";
    return 0;
}
