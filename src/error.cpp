#include <checkpointstore/base/error.hpp>

#include <string>
#include <utility>

namespace checkpointstore {

const char* to_string(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::kNone: return "none";
        case ErrorCode::kInvalidArgument: return "invalid_argument";
        case ErrorCode::kNotFound: return "not_found";
        case ErrorCode::kAlreadyExists: return "already_exists";
        case ErrorCode::kConflict: return "conflict";
        case ErrorCode::kStaleAuthority: return "stale_authority";
        case ErrorCode::kStaleGeneration: return "stale_generation";
        case ErrorCode::kPermissionDenied: return "permission_denied";
        case ErrorCode::kNotSupported: return "not_supported";
        case ErrorCode::kCapacityExceeded: return "capacity_exceeded";
        case ErrorCode::kIntegrity: return "integrity";
        case ErrorCode::kCorrupt: return "corrupt";
        case ErrorCode::kChecksumMismatch: return "checksum_mismatch";
        case ErrorCode::kDigestMismatch: return "digest_mismatch";
        case ErrorCode::kTruncated: return "truncated";
        case ErrorCode::kBadMagic: return "bad_magic";
        case ErrorCode::kUnsupportedVersion: return "unsupported_version";
        case ErrorCode::kMalformed: return "malformed";
        case ErrorCode::kMissing: return "missing";
        case ErrorCode::kOverflow: return "overflow";
        case ErrorCode::kIo: return "io";
        case ErrorCode::kBackendUnavailable: return "backend_unavailable";
        case ErrorCode::kTransaction: return "transaction";
        case ErrorCode::kGcConcurrency: return "gc_concurrency";
        case ErrorCode::kQuarantined: return "quarantined";
        case ErrorCode::kRetry: return "retry";
        case ErrorCode::kDeadlock: return "deadlock";
        case ErrorCode::kInternal: return "internal";
    }
    return "unknown";
}

bool is_error(ErrorCode code) noexcept { return code != ErrorCode::kNone; }

CheckpointStoreError::CheckpointStoreError(ErrorCode code, std::string message)
    : std::runtime_error(std::move(message)), code_(code), context_() {}

CheckpointStoreError::CheckpointStoreError(ErrorCode code, std::string message, std::string context)
    : std::runtime_error(std::move(message)), code_(code), context_(std::move(context)) {}

[[noreturn]] void throw_error(ErrorCode code, const char* message) {
    throw CheckpointStoreError(code, std::string(message));
}

[[noreturn]] void throw_error(ErrorCode code, std::string message) {
    throw CheckpointStoreError(code, std::move(message));
}

}  // namespace checkpointstore
