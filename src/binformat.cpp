#include "binformat.hpp"

#include <cstring>

namespace checkpointstore {

namespace {

void put_le(Bytes& out, std::uint64_t v, int width) {
    for (int i = 0; i < width; ++i) {
        out.push_back(static_cast<Byte>((v >> (i * 8)) & 0xFF));
    }
}

std::uint64_t get_le(ByteView view, std::size_t pos, int width) {
    if (pos + static_cast<std::size_t>(width) > view.size()) {
        throw_error(ErrorCode::kTruncated, "binary reader underflow");
    }
    std::uint64_t v = 0;
    for (int i = 0; i < width; ++i) {
        v |= (static_cast<std::uint64_t>(static_cast<std::uint8_t>(view[pos + static_cast<std::size_t>(i)]))
              << (i * 8));
    }
    return v;
}

}  // namespace

void BinWriter::u8(std::uint8_t v) { out_.push_back(static_cast<Byte>(v)); }
void BinWriter::u16(std::uint16_t v) { put_le(out_, v, 2); }
void BinWriter::u32(std::uint32_t v) { put_le(out_, v, 4); }
void BinWriter::u64(std::uint64_t v) { put_le(out_, v, 8); }
void BinWriter::i64(std::int64_t v) { put_le(out_, static_cast<std::uint64_t>(v), 8); }
void BinWriter::boolean(bool v) { u8(v ? 1 : 0); }

void BinWriter::bytes(ByteView v) {
    u64(v.size());
    out_.insert(out_.end(), v.begin(), v.end());
}

void BinWriter::string(std::string_view v) {
    bytes(ByteView(reinterpret_cast<const Byte*>(v.data()), v.size()));
}

void BinWriter::bytes_fixed(ByteView v) {
    out_.insert(out_.end(), v.begin(), v.end());
}

void BinWriter::sha256(const crypto::Sha256Digest& v) {
    out_.insert(out_.end(), reinterpret_cast<const Byte*>(v.data()),
                reinterpret_cast<const Byte*>(v.data()) + v.size());
}

void BinWriter::time_point(std::chrono::system_clock::time_point tp) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        tp.time_since_epoch())
                        .count();
    i64(static_cast<std::int64_t>(ms));
}

BinReader::BinReader(ByteView view) : view_(view) {}

void BinReader::require(std::size_t n) const {
    if (n > view_.size() - pos_) {
        throw_error(ErrorCode::kTruncated, "binary reader underflow");
    }
}

std::uint8_t BinReader::u8() {
    require(1);
    return static_cast<std::uint8_t>(view_[pos_++]);
}

std::uint16_t BinReader::u16() {
    const auto v = get_le(view_, pos_, 2);
    pos_ += 2;
    return static_cast<std::uint16_t>(v);
}

std::uint32_t BinReader::u32() {
    const auto v = get_le(view_, pos_, 4);
    pos_ += 4;
    return static_cast<std::uint32_t>(v);
}

std::uint64_t BinReader::u64() {
    const auto v = get_le(view_, pos_, 8);
    pos_ += 8;
    return v;
}

std::int64_t BinReader::i64() { return static_cast<std::int64_t>(u64()); }

bool BinReader::boolean() { return u8() != 0; }

ByteView BinReader::bytes() {
    const auto n = static_cast<std::size_t>(u64());
    require(n);
    ByteView out = view_.subspan(pos_, n);
    pos_ += n;
    return out;
}

std::string BinReader::string() {
    const ByteView b = bytes();
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

ByteView BinReader::bytes_fixed(std::size_t n) {
    require(n);
    ByteView out = view_.subspan(pos_, n);
    pos_ += n;
    return out;
}

crypto::Sha256Digest BinReader::sha256() {
    crypto::Sha256Digest d{};
    const ByteView b = bytes_fixed(32);
    std::memcpy(d.data(), b.data(), 32);
    return d;
}

std::chrono::system_clock::time_point BinReader::time_point() {
    const auto ms = i64();
    return std::chrono::system_clock::time_point(std::chrono::milliseconds(ms));
}

void BinReader::skip(std::size_t n) {
    require(n);
    pos_ += n;
}

std::uint32_t encode_count(std::size_t n) {
    if (n > 0xFFFFFFFFull) {
        throw_error(ErrorCode::kOverflow, "vector size exceeds count field");
    }
    return static_cast<std::uint32_t>(n);
}

std::size_t decode_count(std::uint32_t n, std::size_t max) {
    if (static_cast<std::uint64_t>(n) > static_cast<std::uint64_t>(max)) {
        throw_error(ErrorCode::kMalformed, "count exceeds bound");
    }
    return static_cast<std::size_t>(n);
}

}  // namespace checkpointstore
