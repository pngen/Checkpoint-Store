#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex07");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes data = cpstest_ex::pattern('R', 80 * 1024);
    auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
    store.publish(d, ByteView(data.data(), data.size()));
    auto reps = store.get_replicas(d.id);
    std::cout << "replicas=" << reps.size() << "\n";
    for (const auto& r : reps)
        std::cout << "  replica=" << to_hex_string(r.id.value()) << " role=" << to_string(r.role)
                  << " integrity=" << to_string(r.integrity) << " provenance=" << to_string(r.provenance) << "\n";
    auto state = evaluate_replica_durability(1, reps);
    std::cout << "durability=" << to_string(state) << "\n";
    return 0;
}
