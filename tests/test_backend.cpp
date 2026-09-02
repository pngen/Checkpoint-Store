#include "test_util.hpp"
#include <checkpointstore/storage/local_backend.hpp>
#include <checkpointstore/storage/synthetic_backend.hpp>
#include <checkpointstore/storage/backend.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>

#include <filesystem>
#include <string>

using namespace checkpointstore;

int main() {
    auto root = std::filesystem::temp_directory_path() / "cps_test_backend";
    std::filesystem::remove_all(root);

    CHECK(validate_backend_key("blobs/aa/abc"));
    CHECK(!validate_backend_key(""));
    CHECK(!validate_backend_key("/abs"));
    CHECK(!validate_backend_key("blobs/../x"));
    CHECK(!validate_backend_key("blobs//x"));
    CHECK(!validate_backend_key("C:/foo"));
    CHECK(!validate_backend_key("blobs/./x"));
    { std::string nul("blobs/"); nul.push_back(char(0)); nul.push_back('x'); CHECK(!validate_backend_key(nul)); }

    LocalBackend lb(root / "store", StorageBackendId(1));
    auto digest = crypto::sha256(std::string_view("hello local"));
    BackendKey key{"blobs/aa/" + crypto::hex(digest)};
    lb.put_temp(key, make_bytes("hello local"));
    CHECK(!lb.exists(key));
    lb.commit(key);
    CHECK(lb.exists(key));
    CHECK_EQ(lb.stat(key), 11u);
    auto rd = lb.read(key);
    CHECK(std::string(reinterpret_cast<const char*>(rd.data()), rd.size()) == "hello local");
    CHECK(lb.verify(key, digest) == IntegrityState::kVerified);
    CHECK(lb.verify(key, crypto::sha256(std::string_view("wrong"))) == IntegrityState::kCorrupt);
    CHECK(lb.verify(BackendKey{"blobs/aa/missing"}, digest) == IntegrityState::kMissing);
    CHECK(lb.remove(key));
    CHECK(!lb.exists(key));

    CHECK(lb.health());
    CHECK(lb.descriptor().tier_class == StorageTierClass::kLocalFilesystem);
    CHECK(lb.capabilities().persistent());
    CHECK(lb.capabilities().atomic_rename());
    CHECK(lb.capabilities().fsync());

    bool threw = false;
    try { lb.commit(BackendKey{"blobs/aa/nope"}); } catch (const CheckpointStoreError& e) { threw = e.code() == ErrorCode::kTransaction; }
    CHECK(threw);

    SyntheticBackend syn(StorageTierClass::kSyntheticRemote, StorageTierId(2), StorageBackendId(9), "syn");
    syn.put_temp(BackendKey{"objects/z"}, make_bytes("x"));
    syn.commit(BackendKey{"objects/z"});
    CHECK(syn.exists(BackendKey{"objects/z"}));
    CHECK_EQ(syn.stat(BackendKey{"objects/z"}), 1u);
    CHECK(syn.descriptor().provenance == Provenance::kSynthetic);
    auto meta = syn.query_capacity();
    CHECK(meta.provenance == Provenance::kSynthetic);
    CHECK(syn.verify(BackendKey{"objects/z"}, crypto::sha256(std::string_view("x"))) == IntegrityState::kVerified);
    syn.set_corrupt(BackendKey{"objects/z"});
    CHECK(syn.verify(BackendKey{"objects/z"}, crypto::sha256(std::string_view("x"))) == IntegrityState::kCorrupt);

    std::filesystem::remove_all(root);
    return cpstest::finish("test_backend");
}
