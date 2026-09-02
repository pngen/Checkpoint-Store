#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/base/error.hpp>

#include <filesystem>

using namespace checkpointstore;

static Bytes pat(char c, std::size_t n){ Bytes b(n); for(auto& x:b) x=Byte(c); return b; }

int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_test_manifest";
    std::filesystem::remove_all(root);
    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(3); o.chunk_size = 128 * 1024;
    CheckpointStore s(o);
    s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    auto fam = s.create_family(OwnerId(1));

    // 5 distinct 128KiB chunks -> deterministic ascending offsets.
    Bytes data;
    for (char c='A'; c<='E'; ++c) { auto p=pat(c,128*1024); data.insert(data.end(), p.begin(), p.end()); }
    auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
    auto pub = s.publish(d, ByteView(data.data(), data.size()));
    auto m = pub.manifest;
    CHECK_EQ(m.chunks.size(), 5u);
    // Chunk order deterministic ascending and gap-free.
    std::uint64_t next = 0;
    for (auto& e : m.chunks) {
        CHECK_EQ(e.logical_offset, next);
        next += 128 * 1024;
    }
    CHECK_EQ(m.logical_size, data.size());
    CHECK(m.semantic_digest == pub.manifest.semantic_digest);
    // checkpoint_digest == sha256 of the logical byte stream.
    auto expect = crypto::sha256(ByteView(data.data(), data.size()));
    CHECK(m.checkpoint_digest == expect);

    // Manifest survives save/load with identical semantic digest.
    s.save_state();
    CheckpointStore s2(o);
    s2.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    s2.load_state();
    auto m2 = s2.get_manifest(d.id);
    CHECK(m2.semantic_digest == m.semantic_digest);
    CHECK_EQ(m2.chunks.size(), m.chunks.size());

    std::filesystem::remove_all(root);
    return cpstest::finish("test_manifest");
}
