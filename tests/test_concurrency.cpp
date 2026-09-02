#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>

#include <filesystem>
#include <thread>
#include <vector>
#include <atomic>

using namespace checkpointstore;

static Bytes pat(char c, std::size_t n){ Bytes b(n); for(auto& x:b) x=Byte(c); return b; }

int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_test_concurrency";
    std::filesystem::remove_all(root);

    // Concurrent restores of the same committed checkpoint.
    {
        StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(1); o.chunk_size = 128 * 1024;
        CheckpointStore s(o);
        s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
        auto fam = s.create_family(OwnerId(1));
        Bytes data; for (char c='A';c<='D';++c){auto p=pat(c,128*1024); data.insert(data.end(),p.begin(),p.end());}
        auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
        s.publish(d, ByteView(data.data(), data.size()));

        const int T = 4;
        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int i=0;i<T;i++) {
            threads.emplace_back([&, i]() {
                try {
                    auto rc = s.restore(RestoreId(100 + i), d.id);
                    if (rc.bytes == data) ++ok;
                } catch (...) {}
            });
        }
        for (auto& t : threads) t.join();
        CHECK_EQ(ok.load(), T);
    }

    // Concurrent publication to distinct families must all commit.
    {
        StoreOptions o; o.state_path = root / "s2"; o.boot_id = WorkerBootId(2); o.chunk_size = 96 * 1024;
        CheckpointStore s(o);
        s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store2", StorageBackendId(1)));
        const int N = 4;
        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int i=0;i<N;i++) {
            threads.emplace_back([&, i]() {
                try {
                    auto fam = s.create_family(OwnerId(1));
                    Bytes data = pat((char)('A'+i), 100 * 1024);
                    auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
                    auto pub = s.publish(d, ByteView(data.data(), data.size()));
                    CHECK_EQ(pub.dedup_misses, 2u);
                    if (s.verify_checkpoint(d.id) == IntegrityState::kVerified) ++ok;
                } catch (...) {}
            });
        }
        for (auto& t : threads) t.join();
        CHECK_EQ(ok.load(), N);
    }

    // Concurrent dedup table access under an external mutex (no self deadlock).
    {
        DedupTable dt;
        std::mutex m;
        auto d1 = crypto::sha256(std::string_view("concurrent-content"));
        auto d2 = crypto::sha256(std::string_view("other"));
        (void)dt.insert(BlobId(1), d1, 17, IntegrityState::kVerified, BlobGeneration(1));
        (void)dt.insert(BlobId(2), d2, 5, IntegrityState::kVerified, BlobGeneration(1));
        std::atomic<int> ok{0};
        std::vector<std::thread> threads;
        for (int i=0;i<4;i++) {
            threads.emplace_back([&]() {
                for (int j=0;j<50;j++) {
                    std::lock_guard<std::mutex> g(m);
                    auto lk = dt.lookup(d1, 17);
                    if (lk.result == DedupResult::kHit) { (void)dt.add_reference(d1); (void)dt.release_reference(d1); }
                }
                ++ok;
            });
        }
        for (auto& t : threads) t.join();
        CHECK_EQ(ok.load(), 4);
        CHECK_EQ(dt.refcount(d1), 1u);   // unchanged after balanced add/release
    }

    std::filesystem::remove_all(root);
    return cpstest::finish("test_concurrency");
}