#include "test_util.hpp"
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/model.hpp>
#include <string>

using namespace checkpointstore;

int main() {
    // Known SHA-256 vector.
    auto d = crypto::sha256(std::string_view("abc"));
    CHECK_EQ(crypto::hex(d), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    // Empty input SHA-256.
    auto e = crypto::sha256(ByteView());
    CHECK_EQ(crypto::hex(e), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    // Deterministic and stable.
    CHECK(crypto::equal(d, crypto::sha256(std::string_view("abc"))));

    // CRC-32 known value for "hello".
    auto crc = crypto::crc32(make_bytes("hello"));
    CHECK_EQ(static_cast<std::uint64_t>(crc), 907060870u);

    // Content addressing: same content -> same BlobId/ChunkId; different -> different.
    auto d1 = crypto::sha256(std::string_view("same"));
    auto d2 = crypto::sha256(std::string_view("same"));
    auto d3 = crypto::sha256(std::string_view("different"));
    CHECK(blob_id_from_digest(d1) == blob_id_from_digest(d2));
    CHECK(chunk_id_from_digest(d1) == chunk_id_from_digest(d2));
    CHECK(blob_id_from_digest(d1) != blob_id_from_digest(d3));
    // Identity is not std::hash based.
    CHECK(d1 != d3);

    // hex / parse helpers.
    Bytes b = make_bytes("AB");
    CHECK_EQ(hex_string(b), std::string("4142"));
    Bytes parsed;
    CHECK(parse_hex("abcd", parsed));
    CHECK_EQ(hex_string(parsed), std::string("abcd"));
    CHECK(!parse_hex("abc", parsed));   // odd length
    CHECK(!parse_hex("g0", parsed));    // non-hex

    // UNKNOWN never silently becomes VERIFIED (integrity enum ordering).
    CHECK_EQ(static_cast<int>(IntegrityState::kUnknown), 0);
    CHECK(IntegrityState::kUnverified > IntegrityState::kUnknown);
    CHECK(IntegrityState::kVerified > IntegrityState::kUnverified);

    return cpstest::finish("test_integrity");
}
