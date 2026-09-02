#ifndef CHECKPOINTSTORE_TOOLS_PROTO_OPS_HPP
#define CHECKPOINTSTORE_TOOLS_PROTO_OPS_HPP

#include <checkpointstore/protocol/protocol.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include "../src/binformat.hpp"

#include <string>

namespace checkpointstore::proto_ops {

// Authority envelope carried on mutating requests.
struct AuthorityEnvelope {
    std::uint64_t boot = 0;
    std::uint64_t epoch = 0;
    std::uint64_t generation = 0;
};

inline Bytes encode_authority(const AuthorityEnvelope& a) {
    BinWriter w;
    w.u64(a.boot); w.u64(a.epoch); w.u64(a.generation);
    return w.data();
}
inline AuthorityEnvelope decode_authority(ByteView v) {
    BinReader r(v);
    AuthorityEnvelope a;
    a.boot = r.u64(); a.epoch = r.u64(); a.generation = r.u64();
    return a;
}

// A generic ok payload for simple scalar replies.
inline Bytes encode_u64(std::uint64_t v) { BinWriter w; w.u64(v); return w.data(); }

inline Bytes encode_family_reply(std::uint64_t family) { return encode_u64(family); }
inline std::uint64_t decode_family_reply(ByteView v) { return BinReader(v).u64(); }

// Publish payload: authority + id + fill + logical size. The worker generates
// deterministic 512KiB blocks of a fill byte so chunks are reproducible.
inline Bytes encode_publish(const AuthorityEnvelope& a, std::uint64_t family,
                            char fill, std::uint64_t size) {
    BinWriter w;
    w.u64(a.boot); w.u64(a.epoch); w.u64(a.generation);
    w.u64(family);
    w.u8(fill);
    w.u64(size);
    return w.data();
}
struct PublishRequest { AuthorityEnvelope auth; std::uint64_t family=0; char fill='A'; std::uint64_t size=0; };
inline PublishRequest decode_publish(ByteView v) {
    BinReader r(v);
    PublishRequest p;
    p.auth.boot = r.u64(); p.auth.epoch = r.u64(); p.auth.generation = r.u64();
    p.family = r.u64(); p.fill = (char)r.u8(); p.size = r.u64();
    return p;
}
inline Bytes encode_publish_reply(std::uint64_t id, std::uint64_t chunks, std::uint64_t dedup_hits, std::uint64_t dedup_misses) {
    BinWriter w; w.u64(id); w.u64(chunks); w.u64(dedup_hits); w.u64(dedup_misses); return w.data();
}
struct PublishReply { std::uint64_t id=0; std::uint64_t chunks=0; std::uint64_t hits=0; std::uint64_t misses=0; };
inline PublishReply decode_publish_reply(ByteView v) {
    BinReader r(v); PublishReply p; p.id=r.u64(); p.chunks=r.u64(); p.hits=r.u64(); p.misses=r.u64(); return p;
}

inline Bytes encode_verify(std::uint64_t id, std::uint64_t integrity) { BinWriter w; w.u64(id); w.u64(integrity); return w.data(); }
inline std::uint64_t decode_verify_id(ByteView v) { return BinReader(v).u64(); }
inline std::uint64_t decode_integrity_reply(ByteView v) { return BinReader(v).u64(); }

inline Bytes encode_restore(const AuthorityEnvelope& auth, std::uint64_t id) { BinWriter w; w.u64(auth.boot); w.u64(auth.epoch); w.u64(auth.generation); w.u64(id); return w.data(); }
inline std::pair<AuthorityEnvelope,std::uint64_t> decode_restore(ByteView v) { BinReader r(v); AuthorityEnvelope a; a.boot=r.u64(); a.epoch=r.u64(); a.generation=r.u64(); auto id=r.u64(); return {a,id}; }
inline Bytes encode_restore_reply(std::uint64_t integrity, std::uint64_t bytes) { BinWriter w; w.u64(integrity); w.u64(bytes); return w.data(); }
inline std::pair<std::uint64_t,std::uint64_t> decode_restore_reply(ByteView v) {
    BinReader r(v); auto a=r.u64(); auto b=r.u64(); return {a,b};
}

// Nack payload: error_code + message.
inline Bytes encode_nack(ErrorCode code, const std::string& msg) {
    BinWriter w; w.u32(static_cast<std::uint32_t>(code)); w.string(msg); return w.data();
}
struct NackPayload { ErrorCode code = ErrorCode::kNone; std::string message; };
inline NackPayload decode_nack(ByteView v) {
    BinReader r(v); NackPayload n; n.code = static_cast<ErrorCode>(r.u32()); n.message = r.string(); return n;
}

// Register backend payload: boot + root.
inline Bytes encode_register(std::uint64_t boot, const std::string& root) { BinWriter w; w.u64(boot); w.string(root); return w.data(); }
inline std::pair<std::uint64_t,std::string> decode_register(ByteView v) { BinReader r(v); auto b=r.u64(); auto s=r.string(); return {b,s}; }

inline Bytes encode_retain(std::uint64_t family, std::uint64_t latest_n) { BinWriter w; w.u64(family); w.u64(latest_n); return w.data(); }
inline std::pair<std::uint64_t,std::uint64_t> decode_retain(ByteView v) { BinReader r(v); auto f=r.u64(); auto n=r.u64(); return {f,n}; }

}  // namespace checkpointstore::proto_ops
#endif