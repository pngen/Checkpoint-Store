#ifndef CHECKPOINTSTORE_BASE_ERROR_HPP
#define CHECKPOINTSTORE_BASE_ERROR_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace checkpointstore {

// Error categories used throughout the runtime. These are stable, public
// values that callers may switch on. They are intentionally coarse enough to
// be actionable and precise enough to distinguish authoritative failure modes.
enum class ErrorCode : std::uint32_t {
    kNone = 0,
    kInvalidArgument,
    kNotFound,
    kAlreadyExists,
    kConflict,
    kStaleAuthority,
    kStaleGeneration,
    kPermissionDenied,
    kNotSupported,
    kCapacityExceeded,
    kIntegrity,
    kCorrupt,
    kChecksumMismatch,
    kDigestMismatch,
    kTruncated,
    kBadMagic,
    kUnsupportedVersion,
    kMalformed,
    kMissing,
    kOverflow,
    kIo,
    kBackendUnavailable,
    kTransaction,
    kGcConcurrency,
    kQuarantined,
    kRetry,
    kDeadlock,
    kInternal,
};

[[nodiscard]] const char* to_string(ErrorCode code) noexcept;
[[nodiscard]] bool is_error(ErrorCode code) noexcept;

// The canonical error type thrown by Checkpoint Store. It carries a stable
// ErrorCode, a human-readable message, and an optional traced context string.
class CheckpointStoreError : public std::runtime_error {
public:
    CheckpointStoreError(ErrorCode code, std::string message);
    CheckpointStoreError(ErrorCode code, std::string message, std::string context);

    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::string& context() const noexcept { return context_; }

private:
    ErrorCode code_;
    std::string context_;
};

// Throws a CheckpointStoreError with the given code and message if condition
// is false. This is the primary precondition helper used by the runtime.
[[noreturn]] void throw_error(ErrorCode code, const char* message);
[[noreturn]] void throw_error(ErrorCode code, std::string message);

// A lightweight bearer of a value or an error, used for APIs that must not
// throw on routine failures (for example, parse and verify paths).
template <typename T>
class Expected {
public:
    Expected(T value) : ok_(true), value_(std::move(value)) {}
    Expected(ErrorCode code, std::string message)
        : ok_(false), value_(), error_(code, std::move(message)) {}

    [[nodiscard]] bool has_value() const noexcept { return ok_; }
    [[nodiscard]] explicit operator bool() const noexcept { return ok_; }
    [[nodiscard]] const T& value() const {
        if (!ok_) {
            throw_error(error_.code(), error_.what());
        }
        return value_;
    }
    [[nodiscard]] const CheckpointStoreError& error() const noexcept { return error_; }
    [[nodiscard]] ErrorCode code() const noexcept { return ok_ ? ErrorCode::kNone : error_.code(); }

private:
    bool ok_;
    T value_;
    CheckpointStoreError error_;
};

}  // namespace checkpointstore

#endif
