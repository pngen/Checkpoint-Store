#include "example_util.hpp"
#include <fstream>
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex04");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes data = cpstest_ex::pattern('D', 100 * 1024);
    auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
    store.publish(d, ByteView(data.data(), data.size()));
    std::cout << "verify=" << to_string(store.verify_checkpoint(d.id)) << "\n";
    auto chunks = store.get_chunks(d.id);
    auto digest = chunks.front().digest;
    std::string p = (root / "store" / "blobs" / crypto::hex(digest).substr(0,2) / crypto::hex(digest)).string();
    std::ofstream out(p, std::ios::binary | std::ios::trunc); out << "CORRUPT"; out.close();
    std::cout << "after corrupt verify=" << to_string(store.verify_checkpoint(d.id)) << "\n";
    return 0;
}
