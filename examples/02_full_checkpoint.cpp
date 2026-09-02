#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex02");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes data = cpstest_ex::pattern('A', 96 * 1024);
    auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
    auto pub = store.publish(d, ByteView(data.data(), data.size()));
    std::cout << "published " << to_hex_string(pub.descriptor.id) << " lifecycle="
              << to_string(store.lifecycle(d.id)) << " chunks=" << pub.manifest.chunks.size() << "\n";
    return 0;
}
