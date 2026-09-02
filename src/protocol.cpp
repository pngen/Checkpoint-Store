#include <checkpointstore/protocol/protocol.hpp>

#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>

#include <array>
#include <cstring>

namespace checkpointstore {
namespace protocol {

const char* to_string(MessageKind kind) noexcept {
    switch (kind) {
        case MessageKind::kHeartbeat: return "HEARTBEAT";
        case MessageKind::kRegisterBackend: return "REGISTER_BACKEND";
        case MessageKind::kCreateFamily: return "CREATE_FAMILY";
        case MessageKind::kPublish: return "PUBLISH";
        case MessageKind::kVerify: return "VERIFY";
        case MessageKind::kRestore: return "RESTORE";
        case MessageKind::kRetire: return "RETIRE";
        case MessageKind::kGcRun: return "GC_RUN";
        case MessageKind::kSave: return "SAVE";
        case MessageKind::kStatus: return "STATUS";
        case MessageKind::kAdvanceEpoch: return "ADVANCE_EPOCH";
        case MessageKind::kAck: return "ACK";
        case MessageKind::kNack: return "NACK";
        case MessageKind::kBye: return "BYE";
    }
    return "UNKNOWN";
}

Bytes encode_frame(MessageKind kind, ByteView payload) {
    Bytes out;
    out.reserve(4 + 2 + 1 + 4 + payload.size() + 4);
    auto put_le32 = [&](std::uint32_t v) {
        out.push_back(static_cast<Byte>((v >> 0) & 0xFF));
        out.push_back(static_cast<Byte>((v >> 8) & 0xFF));
        out.push_back(static_cast<Byte>((v >> 16) & 0xFF));
        out.push_back(static_cast<Byte>((v >> 24) & 0xFF));
    };
    put_le32(kMagic);
    out.push_back(static_cast<Byte>(kVersion & 0xFF));
    out.push_back(static_cast<Byte>((kVersion >> 8) & 0xFF));
    out.push_back(static_cast<Byte>(kind));
    put_le32(static_cast<std::uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    put_le32(crypto::crc32(payload));
    return out;
}

namespace {
std::uint32_t read_le32(ByteView v, std::size_t pos) {
    if (pos + 4 > v.size()) {
        throw_error(ErrorCode::kTruncated, "protocol frame header truncated");
    }
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(v[pos])) << 0) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(v[pos + 1])) << 8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(v[pos + 2])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(v[pos + 3])) << 24);
}
std::uint16_t read_le16(ByteView v, std::size_t pos) {
    if (pos + 2 > v.size()) {
        throw_error(ErrorCode::kTruncated, "protocol frame header truncated");
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint8_t>(v[pos])) | (static_cast<std::uint8_t>(v[pos + 1]) << 8));
}
}  // namespace

DecodedFrame decode_frame(ByteView view) {
    if (view.size() < 4 + 2 + 1 + 4 + 4) {
        throw_error(ErrorCode::kTruncated, "protocol frame too small");
    }
    const auto magic = read_le32(view, 0);
    if (magic != kMagic) {
        throw_error(ErrorCode::kBadMagic, "protocol magic mismatch");
    }
    const auto version = read_le16(view, 4);
    if (version != kVersion) {
        throw_error(ErrorCode::kUnsupportedVersion, "protocol version unsupported");
    }
    const std::uint8_t raw_kind = static_cast<std::uint8_t>(view[6]);
    if (raw_kind > static_cast<std::uint8_t>(MessageKind::kBye)) {
        throw_error(ErrorCode::kMalformed, "protocol invalid message kind");
    }
    const auto len = read_le32(view, 7);
    if (len > kMaxPayloadBytes) {
        throw_error(ErrorCode::kMalformed, "protocol payload too large");
    }
    const std::size_t header = 4 + 2 + 1 + 4;
    if (view.size() < header + len + 4) {
        throw_error(ErrorCode::kTruncated, "protocol payload truncated");
    }
    const ByteView payload = view.subspan(header, len);
    const auto crc = read_le32(view, header + len);
    if (crypto::crc32(payload) != crc) {
        throw_error(ErrorCode::kChecksumMismatch, "protocol payload checksum mismatch");
    }
    // Trailing garbage detection.
    if (view.size() != header + len + 4) {
        throw_error(ErrorCode::kMalformed, "protocol frame trailing garbage");
    }
    DecodedFrame d;
    d.kind = static_cast<MessageKind>(raw_kind);
    d.payload = Bytes(payload.begin(), payload.end());
    return d;
}

FrameReadResult try_read_frame(ByteView buffer) {
    FrameReadResult res;
    if (buffer.size() < 4 + 2 + 1 + 4 + 4) {
        res.result = FrameRead::kNeedMore;
        return res;
    }
    const auto magic = read_le32(buffer, 0);
    if (magic != kMagic) {
        res.result = FrameRead::kError;
        return res;
    }
    const auto len = read_le32(buffer, 7);
    if (len > kMaxPayloadBytes) {
        res.result = FrameRead::kError;
        return res;
    }
    const std::size_t total = 4 + 2 + 1 + 4 + len + 4;
    if (buffer.size() < total) {
        res.result = FrameRead::kNeedMore;
        return res;
    }
    res.frame = decode_frame(buffer.first(total));
    res.consumed = total;
    res.result = FrameRead::kOk;
    return res;
}

}  // namespace protocol
}  // namespace checkpointstore
