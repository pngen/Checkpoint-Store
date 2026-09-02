#include "test_util.hpp"
#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/base/error.hpp>

#include <filesystem>
#include <fstream>

using namespace checkpointstore;

static Bytes pattern(char c, std::size_t n){ Bytes b(n); for(auto& x:b) x=Byte(c); return b; }

int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_test_persistence";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "store");

    StoreOptions opts;
    opts.state_path = root / "metadata" / "checkpoint-store.state";
    opts.boot_id = WorkerBootId(7);
    opts.chunk_size = 64 * 1024;

    CheckpointStore store(opts);
    store.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    auto fam = store.create_family(OwnerId(1));
    Bytes data = pattern('A', 300 * 1024);
    auto desc = store.make_full_descriptor(fam, OwnerId(1), data.size());
    auto pub = store.publish(desc, ByteView(data.data(), data.size()));
    store.save_state();

    // Reopen a fresh store, load, verify round-trip.
    {
        CheckpointStore s2(opts);
        s2.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
        s2.load_state();
        CHECK(s2.exists(desc.id));
        CHECK(s2.lifecycle(desc.id) == CheckpointLifecycle::kCommitted);
        auto rc = s2.restore(RestoreId(9), desc.id);
        CHECK(rc.bytes == data);
        auto da = s2.dedup_accounting();
        CHECK_EQ(da.logical_bytes, data.size());
    }

    // Corruption tests: rewrite the state file with bad content.
    auto corrupt_expect = [&](const Bytes& bad, ErrorCode ec) {
        std::ofstream out(opts.state_path, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bad.data()), (std::streamsize)bad.size());
        out.close();
        CheckpointStore s3(opts);
        s3.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
        try { s3.load_state(); } catch (const CheckpointStoreError& e) { return e.code() == ec; }
        return false;
    };

    // Read the valid state, then mutate.
    auto read_state = [&]() {
        std::ifstream in(opts.state_path, std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return make_bytes(raw);
    };
    Bytes good = read_state();

    // Bad magic.
    { Bytes b = good; b[0] = Byte(0x00); CHECK(corrupt_expect(b, ErrorCode::kBadMagic)); }
    // Bad version (byte 4,5).
    { Bytes b = good; b[4] = Byte(0x02); CHECK(corrupt_expect(b, ErrorCode::kUnsupportedVersion)); }
    // Checksum mismatch: flip a payload byte in the body region.
    { Bytes b = good; b[20] = Byte(std::uint8_t(b[20]) ^ 0x01); CHECK(corrupt_expect(b, ErrorCode::kChecksumMismatch)); }
    // Truncation.
    { Bytes b(good.begin(), good.end() - 8); CHECK(corrupt_expect(b, ErrorCode::kTruncated)); }
    // Trailing garbage.
    { Bytes b = good; b.push_back(Byte(0x00)); CHECK(corrupt_expect(b, ErrorCode::kMalformed)); }

    // Restore valid state so subsequent checks operate on a good store.
    { std::ofstream out(opts.state_path, std::ios::binary | std::ios::trunc); out.write(reinterpret_cast<const char*>(good.data()), (std::streamsize)good.size()); out.close(); }

    // Chunk corruption detected by verify_checkpoint (do not silently repair).
    {
        CheckpointStore s4(opts);
        s4.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
        s4.load_state();
        auto chunks = s4.get_chunks(desc.id);
        CHECK(!chunks.empty());
        // Corrupt the very first blob.
        auto digest = chunks.front().digest;
        std::string blob_path = (root / "store" / "blobs" / crypto::hex(digest).substr(0,2) / crypto::hex(digest)).string();
        std::ofstream out(blob_path, std::ios::binary | std::ios::trunc);
        out << "CORRUPTED";
        out.close();
        CHECK(s4.verify_checkpoint(desc.id) == IntegrityState::kCorrupt);
        // Restore from a sole corrupt source must fail explicitly.
        bool restored = false;
        try { auto rc = s4.restore(RestoreId(10), desc.id); (void)rc; restored = true; }
        catch (const CheckpointStoreError& e) { restored = (e.code() == ErrorCode::kCorrupt); }
        CHECK(restored);
    }

    std::filesystem::remove_all(root);
    return cpstest::finish("test_persistence");
}
