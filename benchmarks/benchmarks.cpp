#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/storage/dedup.hpp>
#include <checkpointstore/storage/chunking.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/protocol/protocol.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace checkpointstore;

static std::uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
static Bytes pattern(char c, std::size_t n){ Bytes b(n); for(auto&x:b) x=Byte(c); return b; }
static void report(const char* name, std::uint64_t ops, std::uint64_t ns, std::uint64_t bytes = 0) {
    double us = (double)ns / 1000.0;
    double ops_per_s = ops ? (1000.0 * (double)ops / us) : 0.0;
    std::cout << name << ": " << ops << " ops in " << us << " us => " << ops_per_s << " ops/s";
    if (bytes) {
        double mb = (double)bytes / (1024.0 * 1024.0);
        double s = (double)ns / 1e9;
        std::cout << ", " << mb << " MiB, " << (mb / s) << " MiB/s";
    }
    std::cout << "\n";
}

int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_bench";
    std::filesystem::remove_all(root);

    // SHA-256 hashing.
    Bytes data = pattern('H', 8 * 1024 * 1024);
    {
        std::uint64_t t0 = now_ns();
        std::uint64_t n = 0;
        for (int i = 0; i < 20; ++i) { auto d = crypto::sha256(ByteView(data.data(), data.size())); (void)d; ++n; }
        std::uint64_t t1 = now_ns();
        report("sha256", n, t1 - t0, data.size() * n);
    }
    // Fixed-size chunking.
    {
        std::uint64_t t0 = now_ns();
        std::uint64_t n = 0;
        for (int i = 0; i < 20; ++i) { auto s = chunk_stream(ByteView(data.data(), data.size()), 64 * 1024); n += s.size(); }
        std::uint64_t t1 = now_ns();
        report("chunking", n, t1 - t0, data.size() * 20);
    }
    // Dedup lookup.
    {
        DedupTable dt;
        auto d = crypto::sha256(std::string_view("dedup-key"));
        (void)dt.insert(BlobId(1), d, 9, IntegrityState::kVerified, BlobGeneration(1));
        std::uint64_t t0 = now_ns();
        std::uint64_t n = 0;
        for (int i = 0; i < 200000; ++i) { auto lk = dt.lookup(d, 9); if (lk.result==DedupResult::kHit) ++n; }
        std::uint64_t t1 = now_ns();
        report("dedup_lookup", n, t1 - t0);
    }
    // Protocol encode/decode.
    {
        Bytes payload = pattern('P', 4096);
        Bytes frame = protocol::encode_frame(protocol::MessageKind::kPublish, ByteView(payload.data(), payload.size()));
        std::uint64_t t0 = now_ns();
        std::uint64_t n = 0;
        for (int i = 0; i < 100000; ++i) { auto dec = protocol::decode_frame(ByteView(frame.data(), frame.size())); if (dec.kind==protocol::MessageKind::kPublish) ++n; }
        std::uint64_t t1 = now_ns();
        report("protocol_decode", n, t1 - t0);
    }
    // Manifest creation (canonicalization) via a full publish metadata path.
    {
        StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(1); o.chunk_size = 64 * 1024;
        CheckpointStore store(o);
        store.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
        auto fam = store.create_family(OwnerId(1));
        Bytes cp = pattern('C', 4 * 1024 * 1024);
        auto d = store.make_full_descriptor(fam, OwnerId(1), cp.size());
        std::uint64_t t0 = now_ns();
        auto pub = store.publish(d, ByteView(cp.data(), cp.size()));
        std::uint64_t t1 = now_ns();
        report("publish_metadata_path", 1, t1 - t0, cp.size());
        // Restore.
        std::uint64_t t2 = now_ns();
        auto rc = store.restore(RestoreId(1), d.id);
        std::uint64_t t3 = now_ns();
        report("restore", 1, t3 - t2, rc.bytes.size());
    }
    // Real local I/O sequential write/read at larger size.
    {
        StoreOptions o; o.state_path = root / "s2"; o.boot_id = WorkerBootId(2); o.chunk_size = 60 * 1024;
        CheckpointStore store(o);
        store.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store2", StorageBackendId(1)));
        auto fam = store.create_family(OwnerId(1));
        Bytes big = pattern('W', 64 * 1024 * 1024);
        auto d = store.make_full_descriptor(fam, OwnerId(1), big.size());
        std::uint64_t t0 = now_ns();
        auto pub = store.publish(d, ByteView(big.data(), big.size()));
        std::uint64_t t1 = now_ns();
        std::cout << "local sequential checkpoint write: " << (double)big.size()/(1024.0*1024.0) << " MiB in "
                  << (double)(t1-t0)/1e6 << " ms = " << ((double)big.size()/(1024.0*1024.0)) / ((double)(t1-t0)/1e9)
                  << " MiB/s (LOCAL_FILESYSTEM, may include OS caching)\n";
        std::uint64_t t2 = now_ns();
        auto rc = store.restore(RestoreId(2), d.id);
        std::uint64_t t3 = now_ns();
        std::cout << "local sequential checkpoint read: " << (double)rc.bytes.size()/(1024.0*1024.0) << " MiB in "
                  << (double)(t3-t2)/1e6 << " ms = " << ((double)rc.bytes.size()/(1024.0*1024.0)) / ((double)(t3-t2)/1e9)
                  << " MiB/s (LOCAL_FILESYSTEM observed)\n";
        std::cout << "chunk_size=" << o.chunk_size << " chunk_count=" << pub.manifest.chunks.size()
                  << " dedup_ratio=" << (pub.manifest.logical_size / (double)pub.unique_physical_bytes) << "\n";
    }

    std::filesystem::remove_all(root);
    return 0;
}
