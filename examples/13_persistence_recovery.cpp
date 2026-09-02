#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex13");
    {
        // First process: save state.
        auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
        auto fam = store.create_family(OwnerId(1));
        Bytes data = cpstest_ex::pattern('P', 96 * 1024);
        auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
        store.publish(d, ByteView(data.data(), data.size()));
        store.save_state();
    }
    // Recover in a fresh process incarnation.
    auto store2 = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    store2.load_state();
    auto ids = store2.list_checkpoints();
    std::cout << "recovered checkpoints=" << ids.size() << "\n";
    for (auto id : ids) std::cout << "  " << to_hex_string(id.value()) << "\n";
    return 0;
}
