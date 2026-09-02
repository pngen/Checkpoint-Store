#include "example_util.hpp"
#include <cassert>
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex08");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes data = cpstest_ex::pattern('S', 128 * 1024);
    auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
    store.publish(d, ByteView(data.data(), data.size()));
    auto rc = store.restore(RestoreId(1), d.id);
    std::cout << "restored " << rc.bytes.size() << " bytes, parity=" << (rc.bytes == data)
              << " integrity=" << to_string(rc.integrity)
              << " source_replica=" << rc.evidence.source_replica.value()
              << " dur_ms=" << rc.evidence.duration.count() << "\n";
    return 0;
}
