#include "example_util.hpp"
// This example restores a deterministic checkpoint into a CPU buffer, which is
// the reference path for the LOCAL_CHECKPOINT_TO_CUDA_RESTORE proof. The real
// accelerator path is enabled only with CHECKPOINTSTORE_ENABLE_CUDA_PROOF and
// is NOT GPUDirect Storage: it is an ordinary host->device (H2D) copy after a
// verified CPU restore.
int main() {
    using namespace checkpointstore;
    auto root = cpstest_ex::temp_root("cps_ex15");
    auto store = cpstest_ex::make_store(root, WorkerBootId(1), 32 * 1024);
    auto fam = store.create_family(OwnerId(1));
    Bytes data = cpstest_ex::pattern('C', 96 * 1024);
    auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
    store.publish(d, ByteView(data.data(), data.size()));
    auto rc = store.restore(RestoreId(1), d.id);
    std::cout << "CPU reference restored " << rc.bytes.size() << " bytes, integrity="
              << to_string(rc.integrity) << "\n";
#ifdef CHECKPOINTSTORE_ENABLE_CUDA_PROOF
    std::cout << "CUDA proof is enabled (see test_cuda.cpp for the real H2D/kernel/D2H path).\n";
#else
    std::cout << "CUDA proof is a separate optional build (CHECKPOINTSTORE_ENABLE_CUDA_PROOF).\n";
#endif
    return 0;
}
