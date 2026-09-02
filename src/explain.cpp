#include <checkpointstore/explain.hpp>

#include "impl.hpp"

#include <checkpointstore/base/byte.hpp>
#include <checkpointstore/base/error.hpp>
#include <checkpointstore/crypto/hash.hpp>
#include <checkpointstore/store.hpp>

#include <sstream>

namespace checkpointstore {

std::string CheckpointStore::explain_integrity(CheckpointId id) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    auto it = impl_->checkpoints.find(id);
    if (it == impl_->checkpoints.end()) {
        throw_error(ErrorCode::kNotFound, "checkpoint not found");
    }
    std::ostringstream os;
    os << "Checkpoint " << to_hex_string(id.value()) << " integrity is "
       << to_string(it->second.integrity) << ". ";
    switch (it->second.integrity) {
        case IntegrityState::kVerified:
            os << "All chunk digests and the final checkpoint digest verified.";
            break;
        case IntegrityState::kCorrupt:
            os << "A chunk or the checkpoint digest did not match; do not restore from a "
               << "corrupt sole source.";
            break;
        case IntegrityState::kMissing:
            os << "A required chunk is missing from the backend.";
            break;
        case IntegrityState::kUnknown:
        case IntegrityState::kUnverified:
            os << "Verification has not completed (no evidence yet).";
            break;
    }
    return os.str();
}

std::string explain_checkpoint(const CheckpointDescriptor& d) {
    std::ostringstream os;
    os << "Checkpoint " << to_hex_string(d.id) << " (family " << to_hex_string(d.family_id)
       << ", generation " << d.generation.value() << ") is "
       << to_string(d.kind) << ", " << d.logical_size << " logical bytes, retention "
       << to_string(d.retention) << ", durability " << to_string(d.durability)
       << ", provenance " << to_string(d.provenance) << ", priority "
       << to_string(d.restore_priority) << ".";
    return os.str();
}

std::string explain_manifest(const CheckpointManifest& m) {
    std::ostringstream os;
    os << "Manifest " << to_hex_string(m.id) << " covers " << m.chunks.size()
       << " chunks (" << m.logical_size << " bytes) for checkpoint "
       << to_hex_string(m.checkpoint_id) << " generation " << m.checkpoint_generation.value()
       << "; semantic digest " << crypto::hex(m.semantic_digest) << ".";
    return os.str();
}

std::string explain_dedup(const DedupEntry& e, bool reused) {
    std::ostringstream os;
    os << "Blob " << to_hex_string(e.blob_id.value()) << " has " << e.refcount
       << " references over " << e.physical_size << " physical bytes, integrity "
       << to_string(e.integrity) << ". ";
    os << (reused ? "Chunk reused because SHA-256 and logical length exactly match an "
                     "existing verified blob."
                  : "Chunk required a new physical blob write.");
    return os.str();
}

std::string explain_replica(const ReplicaDescriptor& r) {
    std::ostringstream os;
    os << "Replica " << to_hex_string(r.id.value()) << " on backend "
       << to_hex_string(r.backend_id.value()) << " (" << to_string(r.tier) << ") is "
       << to_string(r.integrity) << " with role " << to_string(r.role) << ", "
       << r.physical_size << " bytes, provenance " << to_string(r.provenance) << ".";
    return os.str();
}

std::string explain_restore(const RestoreEvidence& ev) {
    std::ostringstream os;
    os << "Restore " << to_hex_string(ev.restore_id.value()) << " of checkpoint "
       << to_hex_string(ev.source_checkpoint.value()) << " restored " << ev.bytes_restored
       << " bytes across " << ev.chunks_restored << " chunks";
    if (ev.fallback_source) {
        os << ", fallback replica " << to_hex_string(ev.fallback_source->value());
    }
    os << ", duration " << ev.duration.count() << " ms, integrity "
       << to_string(ev.integrity) << ".";
    return os.str();
}

std::string explain_retention(const RetentionPolicy& p, bool protected_) {
    std::ostringstream os;
    os << "Retention policy " << to_hex_string(p.id.value()) << " for family "
       << to_hex_string(p.family_id.value()) << " is " << to_string(p.retention_class);
    if (p.retention_class == RetentionClass::kKeepLatestN) {
        os << " (latest " << p.latest_n << ")";
    } else if (p.retention_class == RetentionClass::kTtl) {
        os << " (ttl " << p.ttl.count() << "s)";
    }
    os << (protected_ ? ", protecting this checkpoint." : ", not protecting this checkpoint.");
    return os.str();
}

std::string explain_gc(const GcState& s) {
    std::ostringstream os;
    os << "GC epoch " << to_hex_string(s.epoch.value()) << " generation "
       << s.generation.value() << " is " << to_string(s.phase) << "; "
       << s.reachable_blobs.size() << " reachable blobs, " << s.reclaimable_blobs.size()
       << " reclaimable candidates, " << s.marked_bytes << " marked bytes.";
    return os.str();
}

std::string explain_integrity(IntegrityState state) {
    switch (state) {
        case IntegrityState::kUnknown: return "Integrity is UNKNOWN; not verified.";
        case IntegrityState::kUnverified: return "Integrity is UNVERIFIED; no evidence yet.";
        case IntegrityState::kVerified: return "Integrity is VERIFIED; digest matched.";
        case IntegrityState::kCorrupt: return "Integrity is CORRUPT; digest mismatch or bad bytes.";
        case IntegrityState::kMissing: return "Integrity is MISSING; object absent.";
    }
    return "Integrity state unknown.";
}

}  // namespace checkpointstore