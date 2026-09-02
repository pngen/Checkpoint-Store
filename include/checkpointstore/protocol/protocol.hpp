#ifndef CHECKPOINTSTORE_PROTOCOL_HPP
#define CHECKPOINTSTORE_PROTOCOL_HPP

#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/identity/generations.hpp>
#include <checkpointstore/identity/identities.hpp>
#include <checkpointstore/model.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace checkpointstore {
namespace protocol {

inline constexpr std::uint32_t kMagic = 0x52465343u;              // "CSFR"
inline constexpr std::uint16_t kVersion = 1;
inline constexpr std::uint32_t kMaxPayloadBytes = 16u * 1024u * 1024u;  // 16 MiB

enum class MessageKind : std::uint8_t {
    kHeartbeat = 0,
    kRegisterBackend,
    kCreateFamily,
    kPublish,
    kVerify,
    kRestore,
    kRetire,
    kGcRun,
    kSave,
    kStatus,
    kAdvanceEpoch,
    kAck,       // success response
    kNack,      // error response
    kBye,
};

[[nodiscard]] const char* to_string(MessageKind kind) noexcept;

// A decoded frame part. Rejected frames throw a CheckpointStoreError with the
// appropriate code (bad magic, unsupported version, truncated, checksum,
// invalid enum, malformed).
struct DecodedFrame {
    MessageKind kind;
    Bytes payload;
};

// Encodes a frame with magic, version, kind, payload length, payload, CRC-32.
[[nodiscard]] Bytes encode_frame(MessageKind kind, ByteView payload);

// Decodes a frame, validating magic/version/length/CRC and detecting trailing
// garbage. Throws on any invalid condition.
[[nodiscard]] DecodedFrame decode_frame(ByteView view);

// A bounded read of exactly one frame from a byte buffer, returning how many
// bytes were consumed. Throws on truncation of the header but is resilient to
// an incomplete trailing frame.
enum class FrameRead { kOk, kNeedMore, kError };
struct FrameReadResult {
    FrameRead result;
    DecodedFrame frame;
    std::size_t consumed = 0;
};
[[nodiscard]] FrameReadResult try_read_frame(ByteView buffer);

}  // namespace protocol
}  // namespace checkpointstore

#endif
