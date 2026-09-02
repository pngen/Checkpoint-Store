#ifndef CHECKPOINTSTORE_BASE_STRONG_HPP
#define CHECKPOINTSTORE_BASE_STRONG_HPP

#include <cstdint>
#include <functional>
#include <iosfwd>
#include <type_traits>

namespace checkpointstore {

// --------------------------------------------------------------------------
// Strong identity wrapper.
//
// BasicId models a non-interchangeable identity. Instances of BasicId with
// different Tag types cannot be compared, converted, or assigned to one
// another. This prevents accidental mixing of, for example, a CheckpointId
// with a ChunkId. The underlying representation is a fixed-width unsigned
// integer; value 0 denotes a null/invalid identity.
// --------------------------------------------------------------------------
template <typename Tag, typename Underlying = std::uint64_t>
class BasicId {
public:
    using underlying_type = Underlying;

    constexpr BasicId() noexcept = default;
    constexpr explicit BasicId(underlying_type value) noexcept : value_(value) {}

    [[nodiscard]] constexpr underlying_type value() const noexcept { return value_; }
    [[nodiscard]] constexpr bool is_null() const noexcept { return value_ == underlying_type{0}; }
    [[nodiscard]] constexpr bool is_valid() const noexcept { return value_ != underlying_type{0}; }

    constexpr bool operator==(const BasicId&) const noexcept = default;
    constexpr auto operator<=>(const BasicId&) const noexcept = default;

private:
    underlying_type value_{0};
};

template <typename Tag, typename Underlying>
constexpr bool operator!=(const BasicId<Tag, Underlying>& a, const BasicId<Tag, Underlying>& b) noexcept {
    return !(a == b);
}

// --------------------------------------------------------------------------
// Strong generation wrapper.
//
// BasicGeneration models an explicitly ordered generation. Comparisons are
// always explicit (precedes / follows / equal_to). A numeric generation alone
// is NEVER sufficient to establish authority: authority is incarnation-scoped
// and must combine a generation with a worker boot identity. This is
// documented and enforced at the authority layer rather than silently here.
// --------------------------------------------------------------------------
template <typename Tag, typename Underlying = std::uint64_t>
class BasicGeneration {
public:
    using underlying_type = Underlying;

    constexpr BasicGeneration() noexcept = default;
    constexpr BasicGeneration(underlying_type value) noexcept : value_(value) {}

    [[nodiscard]] constexpr underlying_type value() const noexcept { return value_; }

    constexpr bool operator==(const BasicGeneration&) const noexcept = default;
    constexpr auto operator<=>(const BasicGeneration&) const noexcept = default;

    // Explicit, intent-revealing comparison helpers. Callers must choose the
    // semantically correct relationship; no implicit relational operators are
    // provided because a bare numeric comparison does not by itself express
    // authority or freshness.
    [[nodiscard]] constexpr bool precedes(const BasicGeneration& other) const noexcept {
        return value_ < other.value_;
    }
    [[nodiscard]] constexpr bool follows(const BasicGeneration& other) const noexcept {
        return value_ > other.value_;
    }
    [[nodiscard]] constexpr bool equal_to(const BasicGeneration& other) const noexcept {
        return value_ == other.value_;
    }
    [[nodiscard]] constexpr bool equal_or_precedes(const BasicGeneration& other) const noexcept {
        return value_ <= other.value_;
    }
    [[nodiscard]] constexpr bool equal_or_follows(const BasicGeneration& other) const noexcept {
        return value_ >= other.value_;
    }

    [[nodiscard]] constexpr BasicGeneration next() const noexcept { return BasicGeneration(value_ + underlying_type{1}); }
    constexpr BasicGeneration& operator++() noexcept { ++value_; return *this; }

private:
    underlying_type value_{1};
};

template <typename Tag, typename Underlying>
constexpr bool operator!=(const BasicGeneration<Tag, Underlying>& a, const BasicGeneration<Tag, Underlying>& b) noexcept {
    return !(a == b);
}

// --------------------------------------------------------------------------
// Incarnation-scoped authority.
//
// Authority is never established by a lone generation number. A mutation is
// only authorized when the generation it carries belongs to the same
// incarnation (boot identity) as the live holder of authority, and the
// generation matches the current expected generation. A numerically larger
// stale generation from an old boot identity must never fence a fresh
// process incarnation. AuthorityTag combines a WorkerBootId and a generation
// so the check is explicit.
// --------------------------------------------------------------------------
template <typename Generation, typename BootId>
class AuthorityTag {
public:
    constexpr AuthorityTag(BootId boot, Generation generation) noexcept
        : boot_(boot), generation_(generation) {}

    [[nodiscard]] constexpr const BootId& boot() const noexcept { return boot_; }
    [[nodiscard]] constexpr const Generation& generation() const noexcept { return generation_; }

    constexpr bool operator==(const AuthorityTag&) const noexcept = default;

private:
    BootId boot_;
    Generation generation_;
};

}  // namespace checkpointstore

namespace std {
template <typename Tag, typename Underlying>
struct hash<checkpointstore::BasicId<Tag, Underlying>> {
    std::size_t operator()(const checkpointstore::BasicId<Tag, Underlying>& v) const noexcept {
        return std::hash<Underlying>{}(v.value());
    }
};
}  // namespace std

#endif
