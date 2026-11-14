// Regression tests: a recovered store keeps its own incarnation's authority, and
// an epoch advance in the coordinator revokes the active worker boot for good.
#include "test_util.hpp"

// The coordinator tool's request handling, driven without its socket loop.
#define main checkpoint_store_coordinator_main
#include "../tools/coordinator.cpp"
#undef main

#include <checkpointstore/store.hpp>
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/base/error.hpp>

#include <filesystem>

namespace {

Bytes fill(char c, std::size_t n) { Bytes b(n); for (auto& x : b) x = Byte(c); return b; }

bool stale_refused(CheckpointStore& s, const CheckpointDescriptor& d, const Bytes& data) {
    try {
        s.publish(d, ByteView(data.data(), data.size()));
    } catch (const CheckpointStoreError& e) {
        return e.code() == ErrorCode::kStaleAuthority;
    }
    return false;
}

// After load_state() the recovering process publishes under its own boot; the
// pre-restart incarnation's boot carries no authority.
void test_recovered_store_keeps_own_boot() {
    const auto root = std::filesystem::temp_directory_path() / "cps_test_regression_recover";
    std::filesystem::remove_all(root);
    StoreOptions o; o.state_path = root / "s"; o.boot_id = WorkerBootId(1); o.chunk_size = 128 * 1024;
    const Bytes data = fill('R', 200 * 1024);
    CheckpointFamilyId fam;
    {
        CheckpointStore s(o);
        s.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
        fam = s.create_family(OwnerId(1));
        auto d = s.make_full_descriptor(fam, OwnerId(1), data.size());
        s.publish(d, ByteView(data.data(), data.size()));
        s.save_state();
    }
    StoreOptions fr = o; fr.boot_id = WorkerBootId(100);
    CheckpointStore fresh(fr);
    fresh.register_backend(StorageBackendId(1), std::make_shared<LocalBackend>(root / "store", StorageBackendId(1)));
    fresh.load_state();
    fresh.reset_authority();
    CHECK(fresh.boot_id() == WorkerBootId(100));

    auto stale = fresh.make_full_descriptor(fam, OwnerId(1), data.size());
    stale.producer_boot = WorkerBootId(1);
    CHECK(stale_refused(fresh, stale, data));
    CHECK(!fresh.exists(stale.id));

    auto mine = fresh.make_full_descriptor(fam, OwnerId(1), data.size());
    mine.producer_boot = WorkerBootId(100);
    bool published = false;
    try {
        fresh.publish(mine, ByteView(data.data(), data.size()));
        published = true;
    } catch (const CheckpointStoreError&) {
    }
    CHECK(published);
    std::filesystem::remove_all(root);
}

// The boot whose authority an epoch advance revoked stays revoked when it
// registers again; a fresh boot takes authority.
void test_epoch_advance_revokes_boot() {
    using MK = protocol::MessageKind;
    const auto root = std::filesystem::temp_directory_path() / "cps_test_regression_coordinator";
    std::filesystem::remove_all(root);
    Coordinator c(root, 64 * 1024);
    auto call = [&](MK kind, const Bytes& payload) {
        return c.dispatch(kind, ByteView(payload.data(), payload.size()));
    };
    auto publish_ok = [&](std::uint64_t boot, std::uint64_t family) {
        const proto_ops::AuthorityEnvelope a{boot, c.epoch(), 1};
        try {
            call(MK::kPublish, proto_ops::encode_publish(a, family, 'P', 4096));
            return true;
        } catch (const CheckpointStoreError&) {
            return false;
        }
    };
    call(MK::kRegisterBackend, proto_ops::encode_register(10, "w10"));
    CHECK(c.boot() == 10);
    const auto family = proto_ops::decode_family_reply([&] {
        const Bytes r = call(MK::kCreateFamily, proto_ops::encode_u64(1));
        return r;
    }());
    CHECK(publish_ok(10, family));

    call(MK::kAdvanceEpoch, Bytes{});
    call(MK::kRegisterBackend, proto_ops::encode_register(10, "w10"));   // the revoked boot again
    CHECK(c.boot() != 10);
    CHECK(!publish_ok(10, family));

    call(MK::kRegisterBackend, proto_ops::encode_register(11, "w11"));   // a fresh incarnation
    CHECK(c.boot() == 11);
    CHECK(publish_ok(11, family));
    std::filesystem::remove_all(root);
}

}  // namespace

int main() {
    test_recovered_store_keeps_own_boot();
    test_epoch_advance_revokes_boot();
    return cpstest::finish("test_regression");
}
