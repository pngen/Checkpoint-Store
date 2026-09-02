#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <cuda_runtime.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>
#include <string>
#include <iostream>

using namespace checkpointstore;

// A simple, deterministic transform kernel that exercises real device code.
__global__ void transform_kernel(const unsigned char* in, unsigned char* out, std::size_t n) {
    const std::size_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) {
        unsigned char v = in[i];
        // xorshift-ish deterministic transform.
        v = static_cast<unsigned char>((v * 31u + 7u) ^ 0xA5u);
        out[i] = v;
    }
}

static unsigned char cpu_transform(unsigned char v) {
    return static_cast<unsigned char>((v * 31u + 7u) ^ 0xA5u);
}

static Bytes pattern(char c, std::size_t n){ Bytes b(n); for(auto&x:b) x=Byte(c); return b; }

int main() {
    std::cout.setf(std::ios::unitbuf);
    std::cout << "LOCAL_CHECKPOINT_TO_CUDA_RESTORE\n";
    auto root = std::filesystem::temp_directory_path() / "cps_cuda";
    std::filesystem::remove_all(root);

    // Publish deterministic checkpoint content to the local store.
    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(1); o.chunk_size = 64 * 1024;
    CheckpointStore store(o);
    store.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    auto fam = store.create_family(OwnerId(1));
    Bytes data = pattern('K', 4 * 1024 * 1024);
    auto d = store.make_full_descriptor(fam, OwnerId(1), data.size());
    store.publish(d, ByteView(data.data(), data.size()));

    // Restore verified bytes from the checkpoint (verified CPU path).
    auto rc = store.restore(RestoreId(1), d.id);
    if (rc.integrity != IntegrityState::kVerified) { std::cout << "FAIL: restore not verified\n"; return 1; }
    const std::size_t n = rc.bytes.size();

    // Allocate device buffer, H2D.
    unsigned char* dev_in = nullptr;
    unsigned char* dev_out = nullptr;
    cudaError_t err = cudaMalloc(&dev_in, n);
    if (err != cudaSuccess) { std::cout << "FAIL: cudaMalloc in " << cudaGetErrorString(err) << "\n"; return 1; }
    err = cudaMalloc(&dev_out, n);
    if (err != cudaSuccess) { std::cout << "FAIL: cudaMalloc out\n"; cudaFree(dev_in); return 1; }
    const void* h = rc.bytes.data();
    err = cudaMemcpy(dev_in, h, n, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) { std::cout << "FAIL: H2D " << cudaGetErrorString(err) << "\n"; cudaFree(dev_in); cudaFree(dev_out); return 1; }
    std::cout << "H2D copy of " << n << " bytes to device (ordinary host->device, not GPUDirect Storage)\n";

    // Run the transform kernel.
    const std::size_t block = 256;
    const std::size_t grid = (n + block - 1) / block;
    transform_kernel<<<grid, block>>>(dev_in, dev_out, n);
    err = cudaGetLastError();
    if (err != cudaSuccess) { std::cout << "FAIL: kernel error " << cudaGetErrorString(err) << "\n"; cudaFree(dev_in); cudaFree(dev_out); return 1; }
    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) { std::cout << "FAIL: sync " << cudaGetErrorString(err) << "\n"; cudaFree(dev_in); cudaFree(dev_out); return 1; }

    // D2H.
    std::vector<unsigned char> out(n);
    err = cudaMemcpy(out.data(), dev_out, n, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) { std::cout << "FAIL: D2H\n"; cudaFree(dev_in); cudaFree(dev_out); return 1; }

    // Compare CPU reference.
    bool parity = true;
    for (std::size_t i = 0; i < n; ++i) {
        if (out[i] != cpu_transform(static_cast<unsigned char>(rc.bytes[i]))) { parity = false; break; }
    }
    std::cout << "CPU parity after D2H: " << (parity ? "MATCH" : "MISMATCH") << "\n";

    cudaFree(dev_in);
    cudaFree(dev_out);
    err = cudaDeviceReset();
    err = cudaGetLastError();
    std::cout << "CUDA state after reset: " << (err == cudaSuccess ? "CLEAN" : "DIRTY") << "\n";
    std::cout << "EXPLICIT: this is LOCAL_CHECKPOINT_TO_CUDA_RESTORE; it is NOT GPUDirect Storage and makes no direct storage->GPU DMA claim.\n";

    std::filesystem::remove_all(root);
    return parity ? 0 : 1;
}
