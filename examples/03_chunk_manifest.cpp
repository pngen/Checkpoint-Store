#include "example_util.hpp"
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex03");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes data = cpstest_ex::pattern('C', 100 * 1024);
    auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
    auto pub = store.publish(d, ByteView(data.data(), data.size()));
    std::cout << "manifest " << to_hex_string(pub.manifest.id) << " loops=" << pub.manifest.chunks.size()
              << " logical=" << pub.manifest.logical_size
              << " digest=" << crypto::hex(pub.manifest.checkpoint_digest) << "\n";
    for (const auto& e : pub.manifest.chunks)
        std::cout << "  chunk@" << e.logical_offset << " id=" << to_hex_string(e.chunk_id.value()) << "\n";
    return 0;
}
