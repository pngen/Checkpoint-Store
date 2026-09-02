#include "test_util.hpp"
#include <checkpointstore/protocol/protocol.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>

using namespace checkpointstore;

int main() {
    namespace P = checkpointstore::protocol;
    // Round trip.
    Bytes payload = make_bytes("hello proto");
    auto frame = P::encode_frame(P::MessageKind::kPublish, ByteView(payload.data(), payload.size()));
    auto dec = P::decode_frame(ByteView(frame.data(), frame.size()));
    CHECK(dec.kind == P::MessageKind::kPublish);
    CHECK(dec.payload == payload);

    // try_read_frame behavior.
    {
        auto rd = P::try_read_frame(ByteView(frame.data(), frame.size()));
        CHECK(rd.result == P::FrameRead::kOk);
        CHECK_EQ(rd.consumed, frame.size());
    }
    {
        // Partial frame -> need more.
        auto rd = P::try_read_frame(ByteView(frame.data(), 5));
        CHECK(rd.result == P::FrameRead::kNeedMore);
    }

    auto ex = [&](const Bytes& bad, ErrorCode expected) {
        try { (void)P::decode_frame(ByteView(bad.data(), bad.size())); }
        catch (const CheckpointStoreError& e) { return e.code() == expected; }
        return false;
    };

    // Bad magic.
    {
        Bytes b = frame;
        b[0] = Byte(0x00);
        CHECK(ex(b, ErrorCode::kBadMagic));
    }
    // Unsupported version: bytes 4-5 are version.
    {
        Bytes b = frame;
        b[4] = Byte(0x02);
        CHECK(ex(b, ErrorCode::kUnsupportedVersion));
    }
    // Oversized payload length: bytes 7-10 are length.
    {
        Bytes b = frame;
        b[7] = Byte(0xFF); b[8] = Byte(0xFF); b[9] = Byte(0xFF); b[10] = Byte(0x7F);
        CHECK(ex(b, ErrorCode::kMalformed));
    }
    // Checksum mismatch: corrupt a payload byte.
    {
        Bytes b = frame;
        b[11] = Byte(static_cast<std::uint8_t>(std::uint8_t(b[11]) ^ 0x01));
        CHECK(ex(b, ErrorCode::kChecksumMismatch));
    }
    // Invalid enum kind.
    {
        Bytes b = frame;
        b[6] = Byte(0xFF);
        CHECK(ex(b, ErrorCode::kMalformed));
    }
    // Trailing garbage.
    {
        Bytes b = frame;
        b.push_back(Byte(0x00));
        CHECK(ex(b, ErrorCode::kMalformed));
    }
    // Truncated.
    {
        Bytes b(frame.begin(), frame.end() - 3);
        CHECK(ex(b, ErrorCode::kTruncated));
    }

    return cpstest::finish("test_protocol");
}